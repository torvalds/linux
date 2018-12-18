/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/qed/qede_rdma.h>
#include "qede.h"

static struct qedr_driver *qedr_drv;
static LIST_HEAD(qedr_dev_list);
static DEFINE_MUTEX(qedr_dev_list_lock);

bool qede_rdma_supported(struct qede_dev *dev)
{
	return dev->dev_info.common.rdma_supported;
}

static void _qede_rdma_dev_add(struct qede_dev *edev)
{
	if (!qedr_drv)
		return;

	edev->rdma_info.qedr_dev = qedr_drv->add(edev->cdev, edev->pdev,
						 edev->ndev);
}

static int qede_rdma_create_wq(struct qede_dev *edev)
{
	INIT_LIST_HEAD(&edev->rdma_info.rdma_event_list);
	edev->rdma_info.rdma_wq = create_singlethread_workqueue("rdma_wq");
	if (!edev->rdma_info.rdma_wq) {
		DP_NOTICE(edev, "qedr: Could not create workqueue\n");
		return -ENOMEM;
	}

	return 0;
}

static void qede_rdma_cleanup_event(struct qede_dev *edev)
{
	struct list_head *head = &edev->rdma_info.rdma_event_list;
	struct qede_rdma_event_work *event_node;

	flush_workqueue(edev->rdma_info.rdma_wq);
	while (!list_empty(head)) {
		event_node = list_entry(head->next, struct qede_rdma_event_work,
					list);
		cancel_work_sync(&event_node->work);
		list_del(&event_node->list);
		kfree(event_node);
	}
}

static void qede_rdma_destroy_wq(struct qede_dev *edev)
{
	qede_rdma_cleanup_event(edev);
	destroy_workqueue(edev->rdma_info.rdma_wq);
}

int qede_rdma_dev_add(struct qede_dev *edev)
{
	int rc = 0;

	if (qede_rdma_supported(edev)) {
		rc = qede_rdma_create_wq(edev);
		if (rc)
			return rc;

		INIT_LIST_HEAD(&edev->rdma_info.entry);
		mutex_lock(&qedr_dev_list_lock);
		list_add_tail(&edev->rdma_info.entry, &qedr_dev_list);
		_qede_rdma_dev_add(edev);
		mutex_unlock(&qedr_dev_list_lock);
	}

	return rc;
}

static void _qede_rdma_dev_remove(struct qede_dev *edev)
{
	if (qedr_drv && qedr_drv->remove && edev->rdma_info.qedr_dev)
		qedr_drv->remove(edev->rdma_info.qedr_dev);
	edev->rdma_info.qedr_dev = NULL;
}

void qede_rdma_dev_remove(struct qede_dev *edev)
{
	if (!qede_rdma_supported(edev))
		return;

	qede_rdma_destroy_wq(edev);
	mutex_lock(&qedr_dev_list_lock);
	_qede_rdma_dev_remove(edev);
	list_del(&edev->rdma_info.entry);
	mutex_unlock(&qedr_dev_list_lock);
}

static void _qede_rdma_dev_open(struct qede_dev *edev)
{
	if (qedr_drv && edev->rdma_info.qedr_dev && qedr_drv->notify)
		qedr_drv->notify(edev->rdma_info.qedr_dev, QEDE_UP);
}

static void qede_rdma_dev_open(struct qede_dev *edev)
{
	if (!qede_rdma_supported(edev))
		return;

	mutex_lock(&qedr_dev_list_lock);
	_qede_rdma_dev_open(edev);
	mutex_unlock(&qedr_dev_list_lock);
}

static void _qede_rdma_dev_close(struct qede_dev *edev)
{
	if (qedr_drv && edev->rdma_info.qedr_dev && qedr_drv->notify)
		qedr_drv->notify(edev->rdma_info.qedr_dev, QEDE_DOWN);
}

static void qede_rdma_dev_close(struct qede_dev *edev)
{
	if (!qede_rdma_supported(edev))
		return;

	mutex_lock(&qedr_dev_list_lock);
	_qede_rdma_dev_close(edev);
	mutex_unlock(&qedr_dev_list_lock);
}

static void qede_rdma_dev_shutdown(struct qede_dev *edev)
{
	if (!qede_rdma_supported(edev))
		return;

	mutex_lock(&qedr_dev_list_lock);
	if (qedr_drv && edev->rdma_info.qedr_dev && qedr_drv->notify)
		qedr_drv->notify(edev->rdma_info.qedr_dev, QEDE_CLOSE);
	mutex_unlock(&qedr_dev_list_lock);
}

int qede_rdma_register_driver(struct qedr_driver *drv)
{
	struct qede_dev *edev;
	u8 qedr_counter = 0;

	mutex_lock(&qedr_dev_list_lock);
	if (qedr_drv) {
		mutex_unlock(&qedr_dev_list_lock);
		return -EINVAL;
	}
	qedr_drv = drv;

	list_for_each_entry(edev, &qedr_dev_list, rdma_info.entry) {
		struct net_device *ndev;

		qedr_counter++;
		_qede_rdma_dev_add(edev);
		ndev = edev->ndev;
		if (netif_running(ndev) && netif_oper_up(ndev))
			_qede_rdma_dev_open(edev);
	}
	mutex_unlock(&qedr_dev_list_lock);

	pr_notice("qedr: discovered and registered %d RDMA funcs\n",
		  qedr_counter);

	return 0;
}
EXPORT_SYMBOL(qede_rdma_register_driver);

void qede_rdma_unregister_driver(struct qedr_driver *drv)
{
	struct qede_dev *edev;

	mutex_lock(&qedr_dev_list_lock);
	list_for_each_entry(edev, &qedr_dev_list, rdma_info.entry) {
		if (edev->rdma_info.qedr_dev)
			_qede_rdma_dev_remove(edev);
	}
	qedr_drv = NULL;
	mutex_unlock(&qedr_dev_list_lock);
}
EXPORT_SYMBOL(qede_rdma_unregister_driver);

static void qede_rdma_changeaddr(struct qede_dev *edev)
{
	if (!qede_rdma_supported(edev))
		return;

	if (qedr_drv && edev->rdma_info.qedr_dev && qedr_drv->notify)
		qedr_drv->notify(edev->rdma_info.qedr_dev, QEDE_CHANGE_ADDR);
}

static struct qede_rdma_event_work *
qede_rdma_get_free_event_node(struct qede_dev *edev)
{
	struct qede_rdma_event_work *event_node = NULL;
	struct list_head *list_node = NULL;
	bool found = false;

	list_for_each(list_node, &edev->rdma_info.rdma_event_list) {
		event_node = list_entry(list_node, struct qede_rdma_event_work,
					list);
		if (!work_pending(&event_node->work)) {
			found = true;
			break;
		}
	}

	if (!found) {
		event_node = kzalloc(sizeof(*event_node), GFP_ATOMIC);
		if (!event_node) {
			DP_NOTICE(edev,
				  "qedr: Could not allocate memory for rdma work\n");
			return NULL;
		}
		list_add_tail(&event_node->list,
			      &edev->rdma_info.rdma_event_list);
	}

	return event_node;
}

static void qede_rdma_handle_event(struct work_struct *work)
{
	struct qede_rdma_event_work *event_node;
	enum qede_rdma_event event;
	struct qede_dev *edev;

	event_node = container_of(work, struct qede_rdma_event_work, work);
	event = event_node->event;
	edev = event_node->ptr;

	switch (event) {
	case QEDE_UP:
		qede_rdma_dev_open(edev);
		break;
	case QEDE_DOWN:
		qede_rdma_dev_close(edev);
		break;
	case QEDE_CLOSE:
		qede_rdma_dev_shutdown(edev);
		break;
	case QEDE_CHANGE_ADDR:
		qede_rdma_changeaddr(edev);
		break;
	default:
		DP_NOTICE(edev, "Invalid rdma event %d", event);
	}
}

static void qede_rdma_add_event(struct qede_dev *edev,
				enum qede_rdma_event event)
{
	struct qede_rdma_event_work *event_node;

	if (!edev->rdma_info.qedr_dev)
		return;

	event_node = qede_rdma_get_free_event_node(edev);
	if (!event_node)
		return;

	event_node->event = event;
	event_node->ptr = edev;

	INIT_WORK(&event_node->work, qede_rdma_handle_event);
	queue_work(edev->rdma_info.rdma_wq, &event_node->work);
}

void qede_rdma_dev_event_open(struct qede_dev *edev)
{
	qede_rdma_add_event(edev, QEDE_UP);
}

void qede_rdma_dev_event_close(struct qede_dev *edev)
{
	qede_rdma_add_event(edev, QEDE_DOWN);
}

void qede_rdma_event_changeaddr(struct qede_dev *edev)
{
	qede_rdma_add_event(edev, QEDE_CHANGE_ADDR);
}
