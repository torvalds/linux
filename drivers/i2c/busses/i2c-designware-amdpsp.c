// SPDX-License-Identifier: GPL-2.0

#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/psp-platform-access.h>
#include <linux/psp.h>
#include <linux/workqueue.h>

#include "i2c-designware-core.h"

#define PSP_I2C_RESERVATION_TIME_MS 100

#define PSP_I2C_REQ_RETRY_CNT		400
#define PSP_I2C_REQ_RETRY_DELAY_US	(25 * USEC_PER_MSEC)
#define PSP_I2C_REQ_STS_OK		0x0
#define PSP_I2C_REQ_STS_BUS_BUSY	0x1
#define PSP_I2C_REQ_STS_INV_PARAM	0x3

enum psp_i2c_req_type {
	PSP_I2C_REQ_ACQUIRE,
	PSP_I2C_REQ_RELEASE,
	PSP_I2C_REQ_MAX
};

struct psp_i2c_req {
	struct psp_req_buffer_hdr hdr;
	enum psp_i2c_req_type type;
};

static DEFINE_MUTEX(psp_i2c_access_mutex);
static unsigned long psp_i2c_sem_acquired;
static u32 psp_i2c_access_count;
static bool psp_i2c_mbox_fail;
static struct device *psp_i2c_dev;

static int (*_psp_send_i2c_req)(struct psp_i2c_req *req);

/* Helper to verify status returned by PSP */
static int check_i2c_req_sts(struct psp_i2c_req *req)
{
	u32 status;

	/* Status field in command-response buffer is updated by PSP */
	status = READ_ONCE(req->hdr.status);

	switch (status) {
	case PSP_I2C_REQ_STS_OK:
		return 0;
	case PSP_I2C_REQ_STS_BUS_BUSY:
		return -EBUSY;
	case PSP_I2C_REQ_STS_INV_PARAM:
	default:
		return -EIO;
	}
}

/*
 * Errors in x86-PSP i2c-arbitration protocol may occur at two levels:
 * 1. mailbox communication - PSP is not operational or some IO errors with
 *    basic communication had happened.
 * 2. i2c-requests - PSP refuses to grant i2c arbitration to x86 for too long.
 *
 * In order to distinguish between these in error handling code all mailbox
 * communication errors on the first level (from CCP symbols) will be passed
 * up and if -EIO is returned the second level will be checked.
 */
static int psp_send_i2c_req_cezanne(struct psp_i2c_req *req)
{
	int ret;

	ret = psp_send_platform_access_msg(PSP_I2C_REQ_BUS_CMD, (struct psp_request *)req);
	if (ret == -EIO)
		return check_i2c_req_sts(req);

	return ret;
}

static int psp_send_i2c_req_doorbell(struct psp_i2c_req *req)
{
	int ret;

	ret = psp_ring_platform_doorbell(req->type, &req->hdr.status);
	if (ret == -EIO)
		return check_i2c_req_sts(req);

	return ret;
}

static int psp_send_i2c_req(enum psp_i2c_req_type i2c_req_type)
{
	struct psp_i2c_req *req;
	unsigned long start;
	int status, ret;

	/* Allocate command-response buffer */
	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->hdr.payload_size = sizeof(*req);
	req->type = i2c_req_type;

	start = jiffies;
	ret = read_poll_timeout(_psp_send_i2c_req, status,
				(status != -EBUSY),
				PSP_I2C_REQ_RETRY_DELAY_US,
				PSP_I2C_REQ_RETRY_CNT * PSP_I2C_REQ_RETRY_DELAY_US,
				0, req);
	if (ret) {
		dev_err(psp_i2c_dev, "Timed out waiting for PSP to %s I2C bus\n",
			(i2c_req_type == PSP_I2C_REQ_ACQUIRE) ?
			"release" : "acquire");
		goto cleanup;
	}

	ret = status;
	if (ret) {
		dev_err(psp_i2c_dev, "PSP communication error\n");
		goto cleanup;
	}

	dev_dbg(psp_i2c_dev, "Request accepted by PSP after %ums\n",
		jiffies_to_msecs(jiffies - start));

cleanup:
	if (ret) {
		dev_err(psp_i2c_dev, "Assume i2c bus is for exclusive host usage\n");
		psp_i2c_mbox_fail = true;
	}

	kfree(req);
	return ret;
}

static void release_bus(void)
{
	int status;

	if (!psp_i2c_sem_acquired)
		return;

	status = psp_send_i2c_req(PSP_I2C_REQ_RELEASE);
	if (status)
		return;

	dev_dbg(psp_i2c_dev, "PSP semaphore held for %ums\n",
		jiffies_to_msecs(jiffies - psp_i2c_sem_acquired));

	psp_i2c_sem_acquired = 0;
}

static void psp_release_i2c_bus_deferred(struct work_struct *work)
{
	mutex_lock(&psp_i2c_access_mutex);

	/*
	 * If there is any pending transaction, cannot release the bus here.
	 * psp_release_i2c_bus will take care of this later.
	 */
	if (psp_i2c_access_count)
		goto cleanup;

	release_bus();

cleanup:
	mutex_unlock(&psp_i2c_access_mutex);
}
static DECLARE_DELAYED_WORK(release_queue, psp_release_i2c_bus_deferred);

static int psp_acquire_i2c_bus(void)
{
	int status;

	mutex_lock(&psp_i2c_access_mutex);

	/* Return early if mailbox malfunctioned */
	if (psp_i2c_mbox_fail)
		goto cleanup;

	psp_i2c_access_count++;

	/*
	 * No need to request bus arbitration once we are inside semaphore
	 * reservation period.
	 */
	if (psp_i2c_sem_acquired)
		goto cleanup;

	status = psp_send_i2c_req(PSP_I2C_REQ_ACQUIRE);
	if (status)
		goto cleanup;

	psp_i2c_sem_acquired = jiffies;

	schedule_delayed_work(&release_queue,
			      msecs_to_jiffies(PSP_I2C_RESERVATION_TIME_MS));

	/*
	 * In case of errors with PSP arbitrator psp_i2c_mbox_fail variable is
	 * set above. As a consequence consecutive calls to acquire will bypass
	 * communication with PSP. At any case i2c bus is granted to the caller,
	 * thus always return success.
	 */
cleanup:
	mutex_unlock(&psp_i2c_access_mutex);
	return 0;
}

static void psp_release_i2c_bus(void)
{
	mutex_lock(&psp_i2c_access_mutex);

	/* Return early if mailbox was malfunctional */
	if (psp_i2c_mbox_fail)
		goto cleanup;

	/*
	 * If we are last owner of PSP semaphore, need to release aribtration
	 * via mailbox.
	 */
	psp_i2c_access_count--;
	if (psp_i2c_access_count)
		goto cleanup;

	/*
	 * Send a release command to PSP if the semaphore reservation timeout
	 * elapsed but x86 still owns the controller.
	 */
	if (!delayed_work_pending(&release_queue))
		release_bus();

cleanup:
	mutex_unlock(&psp_i2c_access_mutex);
}

/*
 * Locking methods are based on the default implementation from
 * drivers/i2c/i2c-core-base.c, but with psp acquire and release operations
 * added. With this in place we can ensure that i2c clients on the bus shared
 * with psp are able to lock HW access to the bus for arbitrary number of
 * operations - that is e.g. write-wait-read.
 */
static void i2c_adapter_dw_psp_lock_bus(struct i2c_adapter *adapter,
					unsigned int flags)
{
	psp_acquire_i2c_bus();
	rt_mutex_lock_nested(&adapter->bus_lock, i2c_adapter_depth(adapter));
}

static int i2c_adapter_dw_psp_trylock_bus(struct i2c_adapter *adapter,
					  unsigned int flags)
{
	int ret;

	ret = rt_mutex_trylock(&adapter->bus_lock);
	if (ret)
		return ret;

	psp_acquire_i2c_bus();

	return ret;
}

static void i2c_adapter_dw_psp_unlock_bus(struct i2c_adapter *adapter,
					  unsigned int flags)
{
	psp_release_i2c_bus();
	rt_mutex_unlock(&adapter->bus_lock);
}

static const struct i2c_lock_operations i2c_dw_psp_lock_ops = {
	.lock_bus = i2c_adapter_dw_psp_lock_bus,
	.trylock_bus = i2c_adapter_dw_psp_trylock_bus,
	.unlock_bus = i2c_adapter_dw_psp_unlock_bus,
};

int i2c_dw_amdpsp_probe_lock_support(struct dw_i2c_dev *dev)
{
	struct pci_dev *rdev;

	if (!IS_REACHABLE(CONFIG_CRYPTO_DEV_CCP_DD))
		return -ENODEV;

	if (!dev)
		return -ENODEV;

	if (!(dev->flags & ARBITRATION_SEMAPHORE))
		return -ENODEV;

	/* Allow to bind only one instance of a driver */
	if (psp_i2c_dev)
		return -EEXIST;

	/* Cezanne uses platform mailbox, Mendocino and later use doorbell */
	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (rdev->device == 0x1630)
		_psp_send_i2c_req = psp_send_i2c_req_cezanne;
	else
		_psp_send_i2c_req = psp_send_i2c_req_doorbell;
	pci_dev_put(rdev);

	if (psp_check_platform_access_status())
		return -EPROBE_DEFER;

	psp_i2c_dev = dev->dev;

	dev_info(psp_i2c_dev, "I2C bus managed by AMD PSP\n");

	/*
	 * Install global locking callbacks for adapter as well as internal i2c
	 * controller locks.
	 */
	dev->adapter.lock_ops = &i2c_dw_psp_lock_ops;
	dev->acquire_lock = psp_acquire_i2c_bus;
	dev->release_lock = psp_release_i2c_bus;

	return 0;
}
