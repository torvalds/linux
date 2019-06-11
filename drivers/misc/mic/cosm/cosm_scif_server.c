// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Intel MIC Coprocessor State Management (COSM) Driver
 */
#include <linux/kthread.h>
#include <linux/sched/signal.h>

#include "cosm_main.h"

/*
 * The COSM driver uses SCIF to communicate between the management node and the
 * MIC cards. SCIF is used to (a) Send a shutdown command to the card (b)
 * receive a shutdown status back from the card upon completion of shutdown and
 * (c) receive periodic heartbeat messages from the card used to deduce if the
 * card has crashed.
 *
 * A COSM server consisting of a SCIF listening endpoint waits for incoming
 * connections from the card. Upon acceptance of the connection, a separate
 * work-item is scheduled to handle SCIF message processing for that card. The
 * life-time of this work-item is therefore the time from which the connection
 * from a card is accepted to the time at which the connection is closed. A new
 * work-item starts each time the card boots and is alive till the card (a)
 * shuts down (b) is reset (c) crashes (d) cosm_client driver on the card is
 * unloaded.
 *
 * From the point of view of COSM interactions with SCIF during card
 * shutdown, reset and crash are as follows:
 *
 * Card shutdown
 * -------------
 * 1. COSM client on the card invokes orderly_poweroff() in response to SHUTDOWN
 *    message from the host.
 * 2. Card driver shutdown callback invokes scif_unregister_device(..) resulting
 *    in scif_remove(..) getting called on the card
 * 3. scif_remove -> scif_stop -> scif_handle_remove_node ->
 *    scif_peer_unregister_device -> device_unregister for the host peer device
 * 4. During device_unregister remove(..) method of cosm_client is invoked which
 *    closes the COSM SCIF endpoint on the card. This results in a SCIF_DISCNCT
 *    message being sent to host SCIF. SCIF_DISCNCT message processing on the
 *    host SCIF sets the host COSM SCIF endpoint state to DISCONNECTED and wakes
 *    up the host COSM thread blocked in scif_poll(..) resulting in
 *    scif_poll(..)  returning EPOLLHUP.
 * 5. On the card, scif_peer_release_dev is next called which results in an
 *    SCIF_EXIT message being sent to the host and after receiving the
 *    SCIF_EXIT_ACK from the host the peer device teardown on the card is
 *    complete.
 * 6. As part of the SCIF_EXIT message processing on the host, host sends a
 *    SCIF_REMOVE_NODE to itself corresponding to the card being removed. This
 *    starts a similar SCIF peer device teardown sequence on the host
 *    corresponding to the card being shut down.
 *
 * Card reset
 * ----------
 * The case of interest here is when the card has not been previously shut down
 * since most of the steps below are skipped in that case:

 * 1. cosm_stop(..) invokes hw_ops->stop(..) method of the base PCIe driver
 *    which unregisters the SCIF HW device resulting in scif_remove(..) being
 *    called on the host.
 * 2. scif_remove(..) calls scif_disconnect_node(..) which results in a
 *    SCIF_EXIT message being sent to the card.
 * 3. The card executes scif_stop() as part of SCIF_EXIT message
 *    processing. This results in the COSM endpoint on the card being closed and
 *    the SCIF host peer device on the card getting unregistered similar to
 *    steps 3, 4 and 5 for the card shutdown case above. scif_poll(..) on the
 *    host returns EPOLLHUP as a result.
 * 4. On the host, card peer device unregister and SCIF HW remove(..) also
 *    subsequently complete.
 *
 * Card crash
 * ----------
 * If a reset is issued after the card has crashed, there is no SCIF_DISCNT
 * message from the card which would result in scif_poll(..) returning
 * EPOLLHUP. In this case when the host SCIF driver sends a SCIF_REMOVE_NODE
 * message to itself resulting in the card SCIF peer device being unregistered,
 * this results in a scif_peer_release_dev -> scif_cleanup_scifdev->
 * scif_invalidate_ep call sequence which sets the endpoint state to
 * DISCONNECTED and results in scif_poll(..) returning EPOLLHUP.
 */

#define COSM_SCIF_BACKLOG 16
#define COSM_HEARTBEAT_CHECK_DELTA_SEC 10
#define COSM_HEARTBEAT_TIMEOUT_SEC \
		(COSM_HEARTBEAT_SEND_SEC + COSM_HEARTBEAT_CHECK_DELTA_SEC)
#define COSM_HEARTBEAT_TIMEOUT_MSEC (COSM_HEARTBEAT_TIMEOUT_SEC * MSEC_PER_SEC)

static struct task_struct *server_thread;
static scif_epd_t listen_epd;

/* Publish MIC card's shutdown status to user space MIC daemon */
static void cosm_update_mic_status(struct cosm_device *cdev)
{
	if (cdev->shutdown_status_int != MIC_NOP) {
		cosm_set_shutdown_status(cdev, cdev->shutdown_status_int);
		cdev->shutdown_status_int = MIC_NOP;
	}
}

/* Store MIC card's shutdown status internally when it is received */
static void cosm_shutdown_status_int(struct cosm_device *cdev,
				     enum mic_status shutdown_status)
{
	switch (shutdown_status) {
	case MIC_HALTED:
	case MIC_POWER_OFF:
	case MIC_RESTART:
	case MIC_CRASHED:
		break;
	default:
		dev_err(&cdev->dev, "%s %d Unexpected shutdown_status %d\n",
			__func__, __LINE__, shutdown_status);
		return;
	};
	cdev->shutdown_status_int = shutdown_status;
	cdev->heartbeat_watchdog_enable = false;

	if (cdev->state != MIC_SHUTTING_DOWN)
		cosm_set_state(cdev, MIC_SHUTTING_DOWN);
}

/* Non-blocking recv. Read and process all available messages */
static void cosm_scif_recv(struct cosm_device *cdev)
{
	struct cosm_msg msg;
	int rc;

	while (1) {
		rc = scif_recv(cdev->epd, &msg, sizeof(msg), 0);
		if (!rc) {
			break;
		} else if (rc < 0) {
			dev_dbg(&cdev->dev, "%s: %d rc %d\n",
				__func__, __LINE__, rc);
			break;
		}
		dev_dbg(&cdev->dev, "%s: %d rc %d id 0x%llx\n",
			__func__, __LINE__, rc, msg.id);

		switch (msg.id) {
		case COSM_MSG_SHUTDOWN_STATUS:
			cosm_shutdown_status_int(cdev, msg.shutdown_status);
			break;
		case COSM_MSG_HEARTBEAT:
			/* Nothing to do, heartbeat only unblocks scif_poll */
			break;
		default:
			dev_err(&cdev->dev, "%s: %d unknown msg.id %lld\n",
				__func__, __LINE__, msg.id);
			break;
		}
	}
}

/* Publish crashed status for this MIC card */
static void cosm_set_crashed(struct cosm_device *cdev)
{
	dev_err(&cdev->dev, "node alive timeout\n");
	cosm_shutdown_status_int(cdev, MIC_CRASHED);
	cosm_update_mic_status(cdev);
}

/* Send host time to the MIC card to sync system time between host and MIC */
static void cosm_send_time(struct cosm_device *cdev)
{
	struct cosm_msg msg = { .id = COSM_MSG_SYNC_TIME };
	struct timespec64 ts;
	int rc;

	ktime_get_real_ts64(&ts);
	msg.timespec.tv_sec = ts.tv_sec;
	msg.timespec.tv_nsec = ts.tv_nsec;

	rc = scif_send(cdev->epd, &msg, sizeof(msg), SCIF_SEND_BLOCK);
	if (rc < 0)
		dev_err(&cdev->dev, "%s %d scif_send failed rc %d\n",
			__func__, __LINE__, rc);
}

/*
 * Close this cosm_device's endpoint after its peer endpoint on the card has
 * been closed. In all cases except MIC card crash EPOLLHUP on the host is
 * triggered by the client's endpoint being closed.
 */
static void cosm_scif_close(struct cosm_device *cdev)
{
	/*
	 * Because SHUTDOWN_STATUS message is sent by the MIC cards in the
	 * reboot notifier when shutdown is still not complete, we notify mpssd
	 * to reset the card when SCIF endpoint is closed.
	 */
	cosm_update_mic_status(cdev);
	scif_close(cdev->epd);
	cdev->epd = NULL;
	dev_dbg(&cdev->dev, "%s %d\n", __func__, __LINE__);
}

/*
 * Set card state to ONLINE when a new SCIF connection from a MIC card is
 * received. Normally the state is BOOTING when the connection comes in, but can
 * be ONLINE if cosm_client driver on the card was unloaded and then reloaded.
 */
static int cosm_set_online(struct cosm_device *cdev)
{
	int rc = 0;

	if (MIC_BOOTING == cdev->state || MIC_ONLINE == cdev->state) {
		cdev->heartbeat_watchdog_enable = cdev->sysfs_heartbeat_enable;
		cdev->epd = cdev->newepd;
		if (cdev->state == MIC_BOOTING)
			cosm_set_state(cdev, MIC_ONLINE);
		cosm_send_time(cdev);
		dev_dbg(&cdev->dev, "%s %d\n", __func__, __LINE__);
	} else {
		dev_warn(&cdev->dev, "%s %d not going online in state: %s\n",
			 __func__, __LINE__, cosm_state_string[cdev->state]);
		rc = -EINVAL;
	}
	/* Drop reference acquired by bus_find_device in the server thread */
	put_device(&cdev->dev);
	return rc;
}

/*
 * Work function for handling work for a SCIF connection from a particular MIC
 * card. It first sets the card state to ONLINE and then calls scif_poll to
 * block on activity such as incoming messages on the SCIF endpoint. When the
 * endpoint is closed, the work function exits, completing its life cycle, from
 * MIC card boot to card shutdown/reset/crash.
 */
void cosm_scif_work(struct work_struct *work)
{
	struct cosm_device *cdev = container_of(work, struct cosm_device,
						scif_work);
	struct scif_pollepd pollepd;
	int rc;

	mutex_lock(&cdev->cosm_mutex);
	if (cosm_set_online(cdev))
		goto exit;

	while (1) {
		pollepd.epd = cdev->epd;
		pollepd.events = EPOLLIN;

		/* Drop the mutex before blocking in scif_poll(..) */
		mutex_unlock(&cdev->cosm_mutex);
		/* poll(..) with timeout on our endpoint */
		rc = scif_poll(&pollepd, 1, COSM_HEARTBEAT_TIMEOUT_MSEC);
		mutex_lock(&cdev->cosm_mutex);
		if (rc < 0) {
			dev_err(&cdev->dev, "%s %d scif_poll rc %d\n",
				__func__, __LINE__, rc);
			continue;
		}

		/* There is a message from the card */
		if (pollepd.revents & EPOLLIN)
			cosm_scif_recv(cdev);

		/* The peer endpoint is closed or this endpoint disconnected */
		if (pollepd.revents & EPOLLHUP) {
			cosm_scif_close(cdev);
			break;
		}

		/* Did we timeout from poll? */
		if (!rc && cdev->heartbeat_watchdog_enable)
			cosm_set_crashed(cdev);
	}
exit:
	dev_dbg(&cdev->dev, "%s %d exiting\n", __func__, __LINE__);
	mutex_unlock(&cdev->cosm_mutex);
}

/*
 * COSM SCIF server thread function. Accepts incoming SCIF connections from MIC
 * cards, finds the correct cosm_device to associate that connection with and
 * schedules individual work items for each MIC card.
 */
static int cosm_scif_server(void *unused)
{
	struct cosm_device *cdev;
	scif_epd_t newepd;
	struct scif_port_id port_id;
	int rc;

	allow_signal(SIGKILL);

	while (!kthread_should_stop()) {
		rc = scif_accept(listen_epd, &port_id, &newepd,
				 SCIF_ACCEPT_SYNC);
		if (rc < 0) {
			if (-ERESTARTSYS != rc)
				pr_err("%s %d rc %d\n", __func__, __LINE__, rc);
			continue;
		}

		/*
		 * Associate the incoming connection with a particular
		 * cosm_device, COSM device ID == SCIF node ID - 1
		 */
		cdev = cosm_find_cdev_by_id(port_id.node - 1);
		if (!cdev)
			continue;
		cdev->newepd = newepd;
		schedule_work(&cdev->scif_work);
	}

	pr_debug("%s %d Server thread stopped\n", __func__, __LINE__);
	return 0;
}

static int cosm_scif_listen(void)
{
	int rc;

	listen_epd = scif_open();
	if (!listen_epd) {
		pr_err("%s %d scif_open failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	rc = scif_bind(listen_epd, SCIF_COSM_LISTEN_PORT);
	if (rc < 0) {
		pr_err("%s %d scif_bind failed rc %d\n",
		       __func__, __LINE__, rc);
		goto err;
	}

	rc = scif_listen(listen_epd, COSM_SCIF_BACKLOG);
	if (rc < 0) {
		pr_err("%s %d scif_listen rc %d\n", __func__, __LINE__, rc);
		goto err;
	}
	pr_debug("%s %d listen_epd set up\n", __func__, __LINE__);
	return 0;
err:
	scif_close(listen_epd);
	listen_epd = NULL;
	return rc;
}

static void cosm_scif_listen_exit(void)
{
	pr_debug("%s %d closing listen_epd\n", __func__, __LINE__);
	if (listen_epd) {
		scif_close(listen_epd);
		listen_epd = NULL;
	}
}

/*
 * Create a listening SCIF endpoint and a server kthread which accepts incoming
 * SCIF connections from MIC cards
 */
int cosm_scif_init(void)
{
	int rc = cosm_scif_listen();

	if (rc) {
		pr_err("%s %d cosm_scif_listen rc %d\n",
		       __func__, __LINE__, rc);
		goto err;
	}

	server_thread = kthread_run(cosm_scif_server, NULL, "cosm_server");
	if (IS_ERR(server_thread)) {
		rc = PTR_ERR(server_thread);
		pr_err("%s %d kthread_run rc %d\n", __func__, __LINE__, rc);
		goto listen_exit;
	}
	return 0;
listen_exit:
	cosm_scif_listen_exit();
err:
	return rc;
}

/* Stop the running server thread and close the listening SCIF endpoint */
void cosm_scif_exit(void)
{
	int rc;

	if (!IS_ERR_OR_NULL(server_thread)) {
		rc = send_sig(SIGKILL, server_thread, 0);
		if (rc) {
			pr_err("%s %d send_sig rc %d\n",
			       __func__, __LINE__, rc);
			return;
		}
		kthread_stop(server_thread);
	}

	cosm_scif_listen_exit();
}
