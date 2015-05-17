/*
 * Linux DHD Bus Module for PCIE
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: dhd_pcie_linux.c 506043 2014-10-02 12:29:45Z $
 */


/* include files */
#include <typedefs.h>
#include <bcmutils.h>
#include <bcmdevs.h>
#include <siutils.h>
#include <hndsoc.h>
#include <hndpmu.h>
#include <sbchipc.h>
#if defined(DHD_DEBUG)
#include <hnd_armtrap.h>
#include <hnd_cons.h>
#endif /* defined(DHD_DEBUG) */
#include <dngl_stats.h>
#include <pcie_core.h>
#include <dhd.h>
#include <dhd_bus.h>
#include <dhd_proto.h>
#include <dhd_dbg.h>
#include <dhdioctl.h>
#include <bcmmsgbuf.h>
#include <pcicfg.h>
#include <dhd_pcie.h>
#include <dhd_linux.h>
#ifdef CONFIG_ARCH_MSM
#ifdef CONFIG_ARCH_MSM8994
#include <linux/msm_pcie.h>
#else
#include <mach/msm_pcie.h>
#endif
#endif /* CONFIG_ARCH_MSM */

#define PCI_CFG_RETRY 		10
#define OS_HANDLE_MAGIC		0x1234abcd	/* Magic # to recognize osh */
#define BCM_MEM_FILENAME_LEN 	24		/* Mem. filename length */

#define OSL_PKTTAG_CLEAR(p) \
do { \
	struct sk_buff *s = (struct sk_buff *)(p); \
	ASSERT(OSL_PKTTAG_SZ == 32); \
	*(uint32 *)(&s->cb[0]) = 0; *(uint32 *)(&s->cb[4]) = 0; \
	*(uint32 *)(&s->cb[8]) = 0; *(uint32 *)(&s->cb[12]) = 0; \
	*(uint32 *)(&s->cb[16]) = 0; *(uint32 *)(&s->cb[20]) = 0; \
	*(uint32 *)(&s->cb[24]) = 0; *(uint32 *)(&s->cb[28]) = 0; \
} while (0)


/* user defined data structures  */

typedef struct dhd_pc_res {
	uint32 bar0_size;
	void* bar0_addr;
	uint32 bar1_size;
	void* bar1_addr;
} pci_config_res, *pPci_config_res;

typedef bool (*dhdpcie_cb_fn_t)(void *);

typedef struct dhdpcie_info
{
	dhd_bus_t	*bus;
	osl_t 			*osh;
	struct pci_dev  *dev;		/* pci device handle */
	volatile char 	*regs;		/* pci device memory va */
	volatile char 	*tcm;		/* pci device memory va */
	uint32			tcm_size;	/* pci device memory size */
	struct pcos_info *pcos_info;
	uint16		last_intrstatus;	/* to cache intrstatus */
	int	irq;
	char pciname[32];
	struct pci_saved_state* state;
#ifdef BCMPCIE_OOB_HOST_WAKE
	void *os_cxt;			/* Pointer to per-OS private data */
#endif /* BCMPCIE_OOB_HOST_WAKE */
} dhdpcie_info_t;


struct pcos_info {
	dhdpcie_info_t *pc;
	spinlock_t lock;
	wait_queue_head_t intr_wait_queue;
	struct timer_list tuning_timer;
	int tuning_timer_exp;
	atomic_t timer_enab;
	struct tasklet_struct tuning_tasklet;
};

#ifdef BCMPCIE_OOB_HOST_WAKE
typedef struct dhdpcie_os_info {
	int			oob_irq_num;	/* valid when hardware or software oob in use */
	unsigned long		oob_irq_flags;	/* valid when hardware or software oob in use */
	bool			oob_irq_registered;
	bool			oob_irq_enabled;
	bool			oob_irq_wake_enabled;
	spinlock_t		oob_irq_spinlock;
	void			*dev;		/* handle to the underlying device */
} dhdpcie_os_info_t;
#endif /* BCMPCIE_OOB_HOST_WAKE */

/* function declarations */
static int __devinit
dhdpcie_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit
dhdpcie_pci_remove(struct pci_dev *pdev);
static int dhdpcie_init(struct pci_dev *pdev);
static irqreturn_t dhdpcie_isr(int irq, void *arg);
/* OS Routine functions for PCI suspend/resume */

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
DEFINE_MUTEX(_dhd_sdio_mutex_lock_);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
#endif 

static int dhdpcie_pci_suspend(struct pci_dev *dev, pm_message_t state);
static int dhdpcie_set_suspend_resume(struct pci_dev *dev, bool state);
static int dhdpcie_pci_resume(struct pci_dev *dev);
static int dhdpcie_resume_dev(struct pci_dev *dev);
static int dhdpcie_suspend_dev(struct pci_dev *dev);
static struct pci_device_id dhdpcie_pci_devid[] __devinitdata = {
	{ vendor: 0x14e4,
	device: PCI_ANY_ID,
	subvendor: PCI_ANY_ID,
	subdevice: PCI_ANY_ID,
	class: PCI_CLASS_NETWORK_OTHER << 8,
	class_mask: 0xffff00,
	driver_data: 0,
	},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, dhdpcie_pci_devid);

static struct pci_driver dhdpcie_driver = {
	node:		{},
	name:		"pcieh",
	id_table:	dhdpcie_pci_devid,
	probe:		dhdpcie_pci_probe,
	remove:		dhdpcie_pci_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	save_state:	NULL,
#endif
	suspend:	dhdpcie_pci_suspend,
	resume:		dhdpcie_pci_resume,
};

int dhdpcie_init_succeeded = FALSE;

static int dhdpcie_set_suspend_resume(struct pci_dev *pdev, bool state)
{
	int ret = 0;
	dhdpcie_info_t *pch = pci_get_drvdata(pdev);
	dhd_bus_t *bus = NULL;

	if (pch) {
		bus = pch->bus;
	}

	/* When firmware is not loaded do the PCI bus */
	/* suspend/resume only */
	if (bus && (bus->dhd->busstate == DHD_BUS_DOWN) &&
#ifdef CONFIG_MACH_UNIVERSAL5433
		/* RB:34285 check_rev() : return 1 - new rev., 0 - old rev. */
		(!check_rev() || (check_rev() && !bus->dhd->dongle_reset)))
#else
		!bus->dhd->dongle_reset)
#endif /* CONFIG_MACH_UNIVERSAL5433 */
		{
			ret = dhdpcie_pci_suspend_resume(bus, state);
			return ret;
		}

	if (bus && ((bus->dhd->busstate == DHD_BUS_SUSPEND)||
		(bus->dhd->busstate == DHD_BUS_DATA)) &&
		(bus->suspended != state)) {

		ret = dhdpcie_bus_suspend(bus, state);
	}
	return ret;
}

static int dhdpcie_pci_suspend(struct pci_dev * pdev, pm_message_t state)
{
	BCM_REFERENCE(state);
	return dhdpcie_set_suspend_resume(pdev, TRUE);
}

static int dhdpcie_pci_resume(struct pci_dev *pdev)
{
	return dhdpcie_set_suspend_resume(pdev, FALSE);
}

static int dhdpcie_suspend_dev(struct pci_dev *dev)
{
	int ret;
	DHD_TRACE_HW4(("%s: Enter\n", __FUNCTION__));
	pci_save_state(dev);
	pci_enable_wake(dev, PCI_D0, TRUE);
	pci_disable_device(dev);
	ret = pci_set_power_state(dev, PCI_D3hot);
	if (ret) {
		DHD_ERROR(("%s: pci_set_power_state error %d\n",
			__FUNCTION__, ret));
	}
	return ret;
}

static int dhdpcie_resume_dev(struct pci_dev *dev)
{
	int err = 0;
	DHD_TRACE_HW4(("%s: Enter\n", __FUNCTION__));
	pci_restore_state(dev);
	err = pci_enable_device(dev);
	if (err) {
		printf("%s:pci_enable_device error %d \n", __FUNCTION__, err);
		return err;
	}
	pci_set_master(dev);
	err = pci_set_power_state(dev, PCI_D0);
	if (err) {
		printf("%s:pci_set_power_state error %d \n", __FUNCTION__, err);
		return err;
	}
	return err;
}

int dhdpcie_pci_suspend_resume(struct dhd_bus *bus, bool state)
{
	int rc;
	struct pci_dev *dev = bus->dev;

	if (state) {
#ifndef BCMPCIE_OOB_HOST_WAKE
		dhdpcie_pme_active(bus->osh, state);
#endif /* BCMPCIE_OOB_HOST_WAKE */
		rc = dhdpcie_suspend_dev(dev);
	} else {
		rc = dhdpcie_resume_dev(dev);
#ifndef BCMPCIE_OOB_HOST_WAKE
		dhdpcie_pme_active(bus->osh, state);
#endif /* BCMPCIE_OOB_HOST_WAKE */
	}
	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0))
static int dhdpcie_device_scan(struct device *dev, void *data)
{
	struct pci_dev *pcidev;
	int *cnt = data;

	pcidev = container_of(dev, struct pci_dev, dev);
	if (pcidev->vendor != 0x14e4)
		return 0;

	DHD_INFO(("Found Broadcom PCI device 0x%04x\n", pcidev->device));
	*cnt += 1;
	if (pcidev->driver && strcmp(pcidev->driver->name, dhdpcie_driver.name))
		DHD_ERROR(("Broadcom PCI Device 0x%04x has allocated with driver %s\n",
			pcidev->device, pcidev->driver->name));

	return 0;
}
#endif /* LINUX_VERSION >= 2.6.0 */

int
dhdpcie_bus_register(void)
{
	int error = 0;


#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0))
	if (!(error = pci_module_init(&dhdpcie_driver)))
		return 0;

	DHD_ERROR(("%s: pci_module_init failed 0x%x\n", __FUNCTION__, error));
#else
	if (!(error = pci_register_driver(&dhdpcie_driver))) {
		bus_for_each_dev(dhdpcie_driver.driver.bus, NULL, &error, dhdpcie_device_scan);
		if (!error) {
			DHD_ERROR(("No Broadcom PCI device enumerated!\n"));
		} else if (!dhdpcie_init_succeeded) {
			DHD_ERROR(("%s: dhdpcie initialize failed.\n", __FUNCTION__));
		} else {
			return 0;
		}

		pci_unregister_driver(&dhdpcie_driver);
		error = BCME_ERROR;
	}
#endif /* LINUX_VERSION < 2.6.0 */

	return error;
}


void
dhdpcie_bus_unregister(void)
{
	pci_unregister_driver(&dhdpcie_driver);
}

int __devinit
dhdpcie_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{

	if (dhdpcie_chipmatch (pdev->vendor, pdev->device)) {
		DHD_ERROR(("%s: chipmatch failed!!\n", __FUNCTION__));
			return -ENODEV;
	}
	printf("PCI_PROBE:  bus %X, slot %X,vendor %X, device %X"
		"(good PCI location)\n", pdev->bus->number,
		PCI_SLOT(pdev->devfn), pdev->vendor, pdev->device);

	if (dhdpcie_init (pdev)) {
		DHD_ERROR(("%s: PCIe Enumeration failed\n", __FUNCTION__));
		return -ENODEV;
	}

#ifdef BCMPCIE_DISABLE_ASYNC_SUSPEND
	/* disable async suspend */
	device_disable_async_suspend(&pdev->dev);
#endif /* BCMPCIE_DISABLE_ASYNC_SUSPEND */

	DHD_TRACE(("%s: PCIe Enumeration done!!\n", __FUNCTION__));
	return 0;
}

int
dhdpcie_detach(dhdpcie_info_t *pch)
{
	if (pch) {
		osl_t *osh = pch->osh;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (!dhd_download_fw_on_driverload)
			pci_load_and_free_saved_state(pch->dev, &pch->state);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
		MFREE(osh, pch, sizeof(dhdpcie_info_t));
	}
	return 0;
}


void __devexit
dhdpcie_pci_remove(struct pci_dev *pdev)
{
	osl_t *osh = NULL;
	dhdpcie_info_t *pch = NULL;
	dhd_bus_t *bus = NULL;
#ifdef PCIE_TX_DEFERRAL
	struct sk_buff *skb;
#endif

	DHD_TRACE(("%s Enter\n", __FUNCTION__));

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	if (mutex_is_locked(&_dhd_sdio_mutex_lock_) == 0) {
		DHD_ERROR(("%s : no mutex held. set lock\n", __FUNCTION__));
	}
	else {
		DHD_ERROR(("%s : mutex is locked!. wait for unlocking\n", __FUNCTION__));
	}
	mutex_lock(&_dhd_sdio_mutex_lock_);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
#endif 

	pch = pci_get_drvdata(pdev);
	bus = pch->bus;
	osh = pch->osh;

#ifdef PCIE_TX_DEFERRAL
	if (bus->tx_wq)
		destroy_workqueue(bus->tx_wq);
	skb = skb_dequeue(&bus->orphan_list);
	while (skb) {
		PKTCFREE(osh, skb, TRUE);
		skb = skb_dequeue(&bus->orphan_list);
	}
#endif

#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
	if (bus)
		msm_pcie_deregister_event(&bus->pcie_event);
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

	dhdpcie_bus_remove_prep(bus);
	dhdpcie_bus_release(bus);
	pci_disable_device(pdev);
#ifdef BCMPCIE_OOB_HOST_WAKE
	/* pcie os info detach */
	MFREE(osh, pch->os_cxt, sizeof(dhdpcie_os_info_t));
#endif /* BCMPCIE_OOB_HOST_WAKE */
	/* pcie info detach */
	dhdpcie_detach(pch);
	/* osl detach */
	osl_detach(osh);

	dhdpcie_init_succeeded = FALSE;

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_unlock(&_dhd_sdio_mutex_lock_);
	DHD_ERROR(("%s : the lock is released.\n", __FUNCTION__));
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
#endif /* LINUX */

	DHD_TRACE(("%s Exit\n", __FUNCTION__));

	return;
}

/* Free Linux irq */
int
dhdpcie_request_irq(dhdpcie_info_t *dhdpcie_info)
{
	dhd_bus_t *bus = dhdpcie_info->bus;
	struct pci_dev *pdev = dhdpcie_info->bus->dev;

	snprintf(dhdpcie_info->pciname, sizeof(dhdpcie_info->pciname),
	    "dhdpcie:%s", pci_name(pdev));
	if (request_irq(pdev->irq, dhdpcie_isr, IRQF_SHARED,
	                dhdpcie_info->pciname, bus) < 0) {
			DHD_ERROR(("%s: request_irq() failed\n", __FUNCTION__));
			return -1;
	}
	bus->irq_registered = TRUE;

	DHD_TRACE(("%s %s\n", __FUNCTION__, dhdpcie_info->pciname));


	return 0; /* SUCCESS */
}

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define PRINTF_RESOURCE	"0x%016llx"
#else
#define PRINTF_RESOURCE	"0x%08x"
#endif

/*

Name:  osl_pci_get_resource

Parametrs:

1: struct pci_dev *pdev   -- pci device structure
2: pci_res                       -- structure containing pci configuration space values


Return value:

int   - Status (TRUE or FALSE)

Description:
Access PCI configuration space, retrieve  PCI allocated resources , updates in resource structure.

 */
int dhdpcie_get_resource(dhdpcie_info_t *dhdpcie_info)
{
	phys_addr_t  bar0_addr, bar1_addr;
	ulong bar1_size;
	struct pci_dev *pdev = NULL;
	pdev = dhdpcie_info->dev;
	do {
		if (pci_enable_device(pdev)) {
			printf("%s: Cannot enable PCI device\n", __FUNCTION__);
			break;
		}
		pci_set_master(pdev);
		bar0_addr = pci_resource_start(pdev, 0);	/* Bar-0 mapped address */
		bar1_addr = pci_resource_start(pdev, 2);	/* Bar-1 mapped address */

		/* read Bar-1 mapped memory range */
		bar1_size = pci_resource_len(pdev, 2);

		if ((bar1_size == 0) || (bar1_addr == 0)) {
			printf("%s: BAR1 Not enabled for this device  size(%ld),"
				" addr(0x"PRINTF_RESOURCE")\n",
				__FUNCTION__, bar1_size, bar1_addr);
			goto err;
		}

		dhdpcie_info->regs = (volatile char *) REG_MAP(bar0_addr, DONGLE_REG_MAP_SIZE);
		dhdpcie_info->tcm_size =
		    (bar1_size < DONGLE_TCM_MAP_SIZE) ? bar1_size : DONGLE_TCM_MAP_SIZE;
		dhdpcie_info->tcm = (volatile char *) REG_MAP(bar1_addr, dhdpcie_info->tcm_size);

		if (!dhdpcie_info->regs || !dhdpcie_info->tcm) {
			DHD_ERROR(("%s:ioremap() failed\n", __FUNCTION__));
			break;
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
		if (!dhd_download_fw_on_driverload) {
			/* Backup PCIe configuration so as to use Wi-Fi on/off process
			 * in case of built in driver
			 */
			pci_save_state(pdev);
			dhdpcie_info->state = pci_store_saved_state(pdev);

			if (dhdpcie_info->state == NULL) {
				DHD_ERROR(("%s pci_store_saved_state returns NULL\n",
					__FUNCTION__));
				REG_UNMAP(dhdpcie_info->regs);
				REG_UNMAP(dhdpcie_info->tcm);
				pci_disable_device(pdev);
				break;
			}
		}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

		DHD_TRACE(("%s:Phys addr : reg space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->regs, bar0_addr));
		DHD_TRACE(("%s:Phys addr : tcm_space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->tcm, bar1_addr));

		return 0; /* SUCCESS  */
	} while (0);
err:
	return -1;  /* FAILURE */
}

int dhdpcie_scan_resource(dhdpcie_info_t *dhdpcie_info)
{

	DHD_TRACE(("%s: ENTER\n", __FUNCTION__));

	do {
		/* define it here only!! */
		if (dhdpcie_get_resource (dhdpcie_info)) {
			DHD_ERROR(("%s: Failed to get PCI resources\n", __FUNCTION__));
			break;
		}
		DHD_TRACE(("%s:Exit - SUCCESS \n",
			__FUNCTION__));

		return 0; /* SUCCESS */

	} while (0);

	DHD_TRACE(("%s:Exit - FAILURE \n", __FUNCTION__));

	return -1; /* FAILURE */

}

#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
void dhdpcie_linkdown_cb(struct msm_pcie_notify *noti)
{
	struct pci_dev *pdev = (struct pci_dev *)noti->user;
	dhdpcie_info_t *pch = NULL;

	if (pdev) {
		pch = pci_get_drvdata(pdev);
		if (pch) {
			dhd_bus_t *bus = pch->bus;
			if (bus) {
				dhd_pub_t *dhd = bus->dhd;
				if (dhd) {
					DHD_ERROR(("%s: Event HANG send up "
						"due to PCIe linkdown\n",
						__FUNCTION__));
					bus->islinkdown = TRUE;
					DHD_OS_WAKE_LOCK(dhd);
					dhd_os_check_hang(dhd, 0, -ETIMEDOUT);
				}
			}
		}
	}

}
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

#ifdef PCIE_TX_DEFERRAL
static void dhd_pcie_create_flow_worker(struct work_struct *worker)
{
	dhd_bus_t *bus;
	struct sk_buff *skb;
	uint16 ifidx, flowid;
	flow_queue_t *queue;
	flow_ring_node_t *flow_ring_node;
	unsigned long flags;

	bus = container_of(worker, dhd_bus_t, create_flow_work);
	skb = skb_dequeue(&bus->orphan_list);
	while (skb) {
		ifidx = DHD_PKTTAG_FLOWID((dhd_pkttag_fr_t*)PKTTAG(skb));
		if (BCME_OK != dhd_flowid_update(bus->dhd, ifidx,
			bus->dhd->flow_prio_map[(PKTPRIO(skb))], skb)) {
			PKTCFREE(bus->dhd->osh, skb, TRUE);
			skb = skb_dequeue(&bus->orphan_list);
			continue;
		}
		flowid = DHD_PKTTAG_FLOWID((dhd_pkttag_fr_t*)PKTTAG(skb));
		flow_ring_node = DHD_FLOW_RING(bus->dhd, flowid);
		queue = &flow_ring_node->queue;
		DHD_FLOWRING_LOCK(flow_ring_node->lock, flags);
		if ((flowid >= bus->dhd->num_flow_rings) ||
			(!flow_ring_node->active) ||
			(flow_ring_node->status == FLOW_RING_STATUS_DELETE_PENDING)) {
			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			DHD_ERROR(("%s: Dropping pkt flowid %d, status %d active %d\n",
				__FUNCTION__, flowid, flow_ring_node->status,
				flow_ring_node->active));
			PKTCFREE(bus->dhd->osh, skb, TRUE);
			skb = skb_dequeue(&bus->orphan_list);
			continue;
		}
		if (BCME_OK != dhd_flow_queue_enqueue(bus->dhd, queue, skb)) {
			DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);
			PKTCFREE(bus->dhd->osh, skb, TRUE);
			skb = skb_dequeue(&bus->orphan_list);
			continue;
		}
		DHD_FLOWRING_UNLOCK(flow_ring_node->lock, flags);

		if (flow_ring_node->status == FLOW_RING_STATUS_OPEN)
			dhd_bus_schedule_queue(bus, flowid, FALSE);

		skb = skb_dequeue(&bus->orphan_list);
	}
}

static void dhd_pcie_delete_flow_worker(struct work_struct *worker)
{
	dhd_bus_t *bus;
	uint16 flowid;

	bus = container_of(worker, dhd_bus_t, delete_flow_work);
	for_each_set_bit(flowid, bus->delete_flow_map, bus->dhd->num_flow_rings) {
		clear_bit(flowid, bus->delete_flow_map);
		dhd_bus_flow_ring_delete_response(bus, flowid, BCME_OK);
	}
}

#endif /* PCIE_TX_DEFERRAL */

#if defined(MULTIPLE_SUPPLICANT)
extern void wl_android_post_init(void); // terence 20120530: fix critical section in dhd_open and dhdsdio_probe
#endif

int dhdpcie_init(struct pci_dev *pdev)
{

	osl_t 				*osh = NULL;
	dhd_bus_t 			*bus = NULL;
	dhdpcie_info_t		*dhdpcie_info =  NULL;
	wifi_adapter_info_t	*adapter = NULL;
#ifdef BCMPCIE_OOB_HOST_WAKE
	dhdpcie_os_info_t	*dhdpcie_osinfo = NULL;
#endif /* BCMPCIE_OOB_HOST_WAKE */

#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	if (mutex_is_locked(&_dhd_sdio_mutex_lock_) == 0) {
		DHD_ERROR(("%s : no mutex held. set lock\n", __FUNCTION__));
	}
	else {
		DHD_ERROR(("%s : mutex is locked!. wait for unlocking\n", __FUNCTION__));
	}
	mutex_lock(&_dhd_sdio_mutex_lock_);
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25)) */
#endif 

	do {
		/* osl attach */
		if (!(osh = osl_attach(pdev, PCI_BUS, FALSE))) {
			DHD_ERROR(("%s: osl_attach failed\n", __FUNCTION__));
			break;
		}

		/* initialize static buffer */
		adapter = dhd_wifi_platform_get_adapter(PCI_BUS, pdev->bus->number,
			PCI_SLOT(pdev->devfn));
		if (adapter != NULL)
			DHD_ERROR(("%s: found adapter info '%s'\n", __FUNCTION__, adapter->name));
		else
			DHD_ERROR(("%s: can't find adapter info for this chip\n", __FUNCTION__));
		osl_static_mem_init(osh, adapter);

		/*  allocate linux spcific pcie structure here */
		if (!(dhdpcie_info = MALLOC(osh, sizeof(dhdpcie_info_t)))) {
			DHD_ERROR(("%s: MALLOC of dhd_bus_t failed\n", __FUNCTION__));
			break;
		}
		bzero(dhdpcie_info, sizeof(dhdpcie_info_t));
		dhdpcie_info->osh = osh;
		dhdpcie_info->dev = pdev;

#ifdef BCMPCIE_OOB_HOST_WAKE
		/* allocate OS speicific structure */
		dhdpcie_osinfo = MALLOC(osh, sizeof(dhdpcie_os_info_t));
		if (dhdpcie_osinfo == NULL) {
			DHD_ERROR(("%s: MALLOC of dhdpcie_os_info_t failed\n",
				__FUNCTION__));
			break;
		}
		bzero(dhdpcie_osinfo, sizeof(dhdpcie_os_info_t));
		dhdpcie_info->os_cxt = (void *)dhdpcie_osinfo;

		/* Initialize host wake IRQ */
		spin_lock_init(&dhdpcie_osinfo->oob_irq_spinlock);
		/* Get customer specific host wake IRQ parametres: IRQ number as IRQ type */
		dhdpcie_osinfo->oob_irq_num = wifi_platform_get_irq_number(adapter,
			&dhdpcie_osinfo->oob_irq_flags);
		if (dhdpcie_osinfo->oob_irq_num < 0) {
			DHD_ERROR(("%s: Host OOB irq is not defined\n", __FUNCTION__));
		}
#endif /* BCMPCIE_OOB_HOST_WAKE */

		/* Find the PCI resources, verify the  */
		/* vendor and device ID, map BAR regions and irq,  update in structures */
		if (dhdpcie_scan_resource(dhdpcie_info)) {
			DHD_ERROR(("%s: dhd_Scan_PCI_Res failed\n", __FUNCTION__));

			break;
		}

		/* Bus initialization */
		bus = dhdpcie_bus_attach(osh, dhdpcie_info->regs,
		    dhdpcie_info->tcm, dhdpcie_info->tcm_size);
		if (!bus) {
			DHD_ERROR(("%s:dhdpcie_bus_attach() failed\n", __FUNCTION__));
			break;
		}

		dhdpcie_info->bus = bus;
		dhdpcie_info->bus->dev = pdev;

#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
		bus->pcie_event.events = MSM_PCIE_EVENT_LINKDOWN;
		bus->pcie_event.user = pdev;
		bus->pcie_event.mode = MSM_PCIE_TRIGGER_CALLBACK;
		bus->pcie_event.callback = dhdpcie_linkdown_cb;
		bus->pcie_event.options = MSM_PCIE_CONFIG_NO_RECOVERY;
		msm_pcie_register_event(&bus->pcie_event);
		bus->islinkdown = FALSE;
#endif /* CONFIG_ARCH_MSM */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

		if (bus->intr) {
			/* Register interrupt callback, but mask it (not operational yet). */
			DHD_INTR(("%s: Registering and masking interrupts\n", __FUNCTION__));
			dhdpcie_bus_intr_disable(bus);

			if (dhdpcie_request_irq(dhdpcie_info)) {
				DHD_ERROR(("%s: request_irq() failed\n", __FUNCTION__));
				break;
			}
		} else {
			bus->pollrate = 1;
			DHD_INFO(("%s: PCIe interrupt function is NOT registered "
				"due to polling mode\n", __FUNCTION__));
		}

#if 0 // terence 20150325: fix for WPA/WPA2 4-way handshake fail in hostapd
		if (dhd_download_fw_on_driverload) {
			if (dhd_bus_start(bus->dhd)) {
				DHD_ERROR(("%s: dhd_bud_start() failed\n", __FUNCTION__));
				break;
			}
		}
#endif

		/* set private data for pci_dev */
		pci_set_drvdata(pdev, dhdpcie_info);

#ifdef PCIE_TX_DEFERRAL
		bus->tx_wq = create_singlethread_workqueue("bcmdhd_tx");
		if (bus->tx_wq == NULL) {
			DHD_ERROR(("%s workqueue creation failed\n", __FUNCTION__));
			break;
		}
		INIT_WORK(&bus->create_flow_work, dhd_pcie_create_flow_worker);
		INIT_WORK(&bus->delete_flow_work, dhd_pcie_delete_flow_worker);
		skb_queue_head_init(&bus->orphan_list);
#endif /* PCIE_TX_DEFERRAL */

		/* Attach to the OS network interface */
		DHD_TRACE(("%s(): Calling dhd_register_if() \n", __FUNCTION__));
		if (dhd_register_if(bus->dhd, 0, TRUE)) {
			DHD_ERROR(("%s(): ERROR.. dhd_register_if() failed\n", __FUNCTION__));
			break;
		}

		dhdpcie_init_succeeded = TRUE;

#if defined(MULTIPLE_SUPPLICANT)
		wl_android_post_init(); // terence 20120530: fix critical section in dhd_open and dhdsdio_probe
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
		mutex_unlock(&_dhd_sdio_mutex_lock_);
		DHD_ERROR(("%s : the lock is released.\n", __FUNCTION__));
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
#endif 

		DHD_TRACE(("%s:Exit - SUCCESS \n", __FUNCTION__));
		return 0;  /* return  SUCCESS  */

	} while (0);
	/* reverse the initialization in order in case of error */

	if (bus)
		dhdpcie_bus_release(bus);

#ifdef BCMPCIE_OOB_HOST_WAKE
	if (dhdpcie_osinfo)
		MFREE(osh, dhdpcie_osinfo, sizeof(dhdpcie_os_info_t));
#endif /* BCMPCIE_OOB_HOST_WAKE */

	if (dhdpcie_info)
		dhdpcie_detach(dhdpcie_info);
	pci_disable_device(pdev);
	if (osh)
		osl_detach(osh);

	dhdpcie_init_succeeded = FALSE;
#if defined(MULTIPLE_SUPPLICANT)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 25))
	mutex_unlock(&_dhd_sdio_mutex_lock_);
	DHD_ERROR(("%s : the lock is released.\n", __FUNCTION__));
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)) */
#endif 

	DHD_TRACE(("%s:Exit - FAILURE \n", __FUNCTION__));

	return -1; /* return FAILURE  */
}

/* Free Linux irq */
void
dhdpcie_free_irq(dhd_bus_t *bus)
{
	struct pci_dev *pdev = NULL;

	DHD_TRACE(("%s: freeing up the IRQ\n", __FUNCTION__));
	if (bus && bus->irq_registered) {
		pdev = bus->dev;
		free_irq(pdev->irq, bus);
		bus->irq_registered = FALSE;
	}
	DHD_TRACE(("%s: Exit\n", __FUNCTION__));
	return;
}

/*

Name:  dhdpcie_isr

Parametrs:

1: IN int irq   -- interrupt vector
2: IN void *arg      -- handle to private data structure

Return value:

Status (TRUE or FALSE)

Description:
Interrupt Service routine checks for the status register,
disable interrupt and queue DPC if mail box interrupts are raised.
*/


irqreturn_t
dhdpcie_isr(int irq, void *arg)
{
	dhd_bus_t *bus = (dhd_bus_t*)arg;
	if (dhdpcie_bus_isr(bus))
		return TRUE;
	else
		return FALSE;
}

int
dhdpcie_start_host_pcieclock(dhd_bus_t *bus)
{
	int ret = 0;
#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	int options = 0;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL)
		return BCME_ERROR;

	if (bus->dev == NULL)
		return BCME_ERROR;

#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus->islinkdown) {
		options = MSM_PCIE_CONFIG_NO_CFG_RESTORE;
	}
	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, bus->dev->bus->number,
		bus->dev, NULL, options);
	if (bus->islinkdown && !ret) {
		msm_pcie_recover_config(bus->dev);
		if (bus->dhd)
			DHD_OS_WAKE_UNLOCK(bus->dhd);
		bus->islinkdown = FALSE;
	}
#else
	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, bus->dev->bus->number,
		bus->dev, NULL, 0);
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	if (ret) {
		DHD_ERROR(("%s Failed to bring up PCIe link\n", __FUNCTION__));
		goto done;
	}

done:
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Exit:\n", __FUNCTION__));
	return ret;
}

int
dhdpcie_stop_host_pcieclock(dhd_bus_t *bus)
{
	int ret = 0;

#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	int options = 0;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL)
		return BCME_ERROR;

	if (bus->dev == NULL)
		return BCME_ERROR;

#ifdef CONFIG_ARCH_MSM
#ifdef SUPPORT_LINKDOWN_RECOVERY
	if (bus->islinkdown)
		options = MSM_PCIE_CONFIG_NO_CFG_RESTORE | MSM_PCIE_CONFIG_LINKDOWN;

	ret = msm_pcie_pm_control(MSM_PCIE_SUSPEND,	bus->dev->bus->number,
		bus->dev, NULL, options);
#else
	ret = msm_pcie_pm_control(MSM_PCIE_SUSPEND,	bus->dev->bus->number,
		bus->dev, NULL, 0);
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	if (ret) {
		DHD_ERROR(("Failed to stop PCIe link\n"));
		goto done;
	}
done:
#endif /* CONFIG_ARCH_MSM */
	DHD_TRACE(("%s Exit:\n", __FUNCTION__));
	return ret;
}

int
dhdpcie_disable_device(dhd_bus_t *bus)
{
	if (bus == NULL)
		return BCME_ERROR;

	if (bus->dev == NULL)
		return BCME_ERROR;

	pci_disable_device(bus->dev);

	return 0;
}

int
dhdpcie_enable_device(dhd_bus_t *bus)
{
	int ret = BCME_ERROR;
	dhdpcie_info_t *pch;

	DHD_TRACE(("%s Enter:\n", __FUNCTION__));

	if (bus == NULL)
		return BCME_ERROR;

	if (bus->dev == NULL)
		return BCME_ERROR;

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL)
		return BCME_ERROR;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
	/* Updated with pci_load_and_free_saved_state to compatible
	 * with kernel 3.14 or higher
	 */
	if (pci_load_and_free_saved_state(bus->dev, &pch->state))
		pci_disable_device(bus->dev);
	else
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)))
	if (pci_load_saved_state(bus->dev, pch->state))
		pci_disable_device(bus->dev);
	else
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) and
		* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
		* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
		*/
	{
		pci_restore_state(bus->dev);
		ret = pci_enable_device(bus->dev);
		if (!ret)
			pci_set_master(bus->dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0))
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0) and
		* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)) && \
		* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
		*/

	if (ret)
		pci_disable_device(bus->dev);

	return ret;
}

int
dhdpcie_alloc_resource(dhd_bus_t *bus)
{
	dhdpcie_info_t *dhdpcie_info;
	phys_addr_t bar0_addr, bar1_addr;
	ulong bar1_size;

	do {
		if (bus == NULL) {
			DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
			break;
		}

		if (bus->dev == NULL) {
			DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
			break;
		}

		dhdpcie_info = pci_get_drvdata(bus->dev);
		if (dhdpcie_info == NULL) {
			DHD_ERROR(("%s: dhdpcie_info is NULL\n", __FUNCTION__));
			break;
		}

		bar0_addr = pci_resource_start(bus->dev, 0);	/* Bar-0 mapped address */
		bar1_addr = pci_resource_start(bus->dev, 2);	/* Bar-1 mapped address */

		/* read Bar-1 mapped memory range */
		bar1_size = pci_resource_len(bus->dev, 2);

		if ((bar1_size == 0) || (bar1_addr == 0)) {
			printf("%s: BAR1 Not enabled for this device size(%ld),"
				" addr(0x"PRINTF_RESOURCE")\n",
				__FUNCTION__, bar1_size, bar1_addr);
			break;
		}

		dhdpcie_info->regs = (volatile char *) REG_MAP(bar0_addr, DONGLE_REG_MAP_SIZE);
		if (!dhdpcie_info->regs) {
			DHD_ERROR(("%s: ioremap() for regs is failed\n", __FUNCTION__));
			break;
		}

		bus->regs = dhdpcie_info->regs;
		dhdpcie_info->tcm_size =
		    (bar1_size < DONGLE_TCM_MAP_SIZE) ? bar1_size : DONGLE_TCM_MAP_SIZE;
		dhdpcie_info->tcm = (volatile char *) REG_MAP(bar1_addr, dhdpcie_info->tcm_size);
		if (!dhdpcie_info->tcm) {
			DHD_ERROR(("%s: ioremap() for regs is failed\n", __FUNCTION__));
			REG_UNMAP(dhdpcie_info->regs);
			bus->regs = NULL;
			break;
		}

		bus->tcm = dhdpcie_info->tcm;
		bus->tcm_size = dhdpcie_info->tcm_size;

		DHD_TRACE(("%s:Phys addr : reg space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->regs, bar0_addr));
		DHD_TRACE(("%s:Phys addr : tcm_space = %p base addr 0x"PRINTF_RESOURCE" \n",
			__FUNCTION__, dhdpcie_info->tcm, bar1_addr));

		return 0;
	} while (0);

	return BCME_ERROR;
}

void
dhdpcie_free_resource(dhd_bus_t *bus)
{
	dhdpcie_info_t *dhdpcie_info;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	dhdpcie_info = pci_get_drvdata(bus->dev);
	if (dhdpcie_info == NULL) {
		DHD_ERROR(("%s: dhdpcie_info is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->regs) {
		REG_UNMAP(dhdpcie_info->regs);
		bus->regs = NULL;
	}

	if (bus->tcm) {
		REG_UNMAP(dhdpcie_info->tcm);
		bus->tcm = NULL;
	}
}

int
dhdpcie_bus_request_irq(struct dhd_bus *bus)
{
	dhdpcie_info_t *dhdpcie_info;
	int ret = 0;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	dhdpcie_info = pci_get_drvdata(bus->dev);
	if (dhdpcie_info == NULL) {
		DHD_ERROR(("%s: dhdpcie_info is NULL\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (bus->intr) {
		/* Register interrupt callback, but mask it (not operational yet). */
		DHD_INTR(("%s: Registering and masking interrupts\n", __FUNCTION__));
		dhdpcie_bus_intr_disable(bus);
		ret = dhdpcie_request_irq(dhdpcie_info);
		if (ret) {
			DHD_ERROR(("%s: request_irq() failed, ret=%d\n",
				__FUNCTION__, ret));
			return ret;
		}
	}

	return ret;
}

#ifdef BCMPCIE_OOB_HOST_WAKE
void dhdpcie_oob_intr_set(dhd_bus_t *bus, bool enable)
{
	unsigned long flags;
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;
	spin_lock_irqsave(&dhdpcie_osinfo->oob_irq_spinlock, flags);
	if ((dhdpcie_osinfo->oob_irq_enabled != enable) &&
		(dhdpcie_osinfo->oob_irq_num > 0)) {
		if (enable)
			enable_irq(dhdpcie_osinfo->oob_irq_num);
		else
			disable_irq_nosync(dhdpcie_osinfo->oob_irq_num);
		dhdpcie_osinfo->oob_irq_enabled = enable;
	}
	spin_unlock_irqrestore(&dhdpcie_osinfo->oob_irq_spinlock, flags);
}

static irqreturn_t wlan_oob_irq(int irq, void *data)
{
	dhd_bus_t *bus;
	DHD_TRACE(("%s: IRQ Triggered\n", __FUNCTION__));
	bus = (dhd_bus_t *)data;
	if (bus->dhd->up && bus->suspended) {
		DHD_OS_OOB_IRQ_WAKE_LOCK_TIMEOUT(bus->dhd, OOB_WAKE_LOCK_TIMEOUT);
	}
	return IRQ_HANDLED;
}

int dhdpcie_oob_intr_register(dhd_bus_t *bus)
{
	int err = 0;
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return -EINVAL;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;
	if (dhdpcie_osinfo->oob_irq_registered) {
		DHD_ERROR(("%s: irq is already registered\n", __FUNCTION__));
		return -EBUSY;
	}

	if (dhdpcie_osinfo->oob_irq_num > 0) {
		DHD_INFO_HW4(("%s OOB irq=%d flags=%X \n", __FUNCTION__,
			(int)dhdpcie_osinfo->oob_irq_num,
			(int)dhdpcie_osinfo->oob_irq_flags));
		err = request_irq(dhdpcie_osinfo->oob_irq_num, wlan_oob_irq,
			dhdpcie_osinfo->oob_irq_flags, "dhdpcie_host_wake",
			bus);
		if (err) {
			DHD_ERROR(("%s: request_irq failed with %d\n",
				__FUNCTION__, err));
			return err;
		}
		err = enable_irq_wake(dhdpcie_osinfo->oob_irq_num);
		if (!err)
			dhdpcie_osinfo->oob_irq_wake_enabled = TRUE;
		dhdpcie_osinfo->oob_irq_enabled = TRUE;
	}

	dhdpcie_osinfo->oob_irq_registered = TRUE;

	return err;
}

void dhdpcie_oob_intr_unregister(dhd_bus_t *bus)
{
	int err = 0;
	dhdpcie_info_t *pch;
	dhdpcie_os_info_t *dhdpcie_osinfo;

	DHD_TRACE(("%s: Enter\n", __FUNCTION__));
	if (bus == NULL) {
		DHD_ERROR(("%s: bus is NULL\n", __FUNCTION__));
		return;
	}

	if (bus->dev == NULL) {
		DHD_ERROR(("%s: bus->dev is NULL\n", __FUNCTION__));
		return;
	}

	pch = pci_get_drvdata(bus->dev);
	if (pch == NULL) {
		DHD_ERROR(("%s: pch is NULL\n", __FUNCTION__));
		return;
	}

	dhdpcie_osinfo = (dhdpcie_os_info_t *)pch->os_cxt;
	if (!dhdpcie_osinfo->oob_irq_registered) {
		DHD_ERROR(("%s: irq is not registered\n", __FUNCTION__));
		return;
	}
	if (dhdpcie_osinfo->oob_irq_num > 0) {
		if (dhdpcie_osinfo->oob_irq_wake_enabled) {
			err = disable_irq_wake(dhdpcie_osinfo->oob_irq_num);
			if (!err)
				dhdpcie_osinfo->oob_irq_wake_enabled = FALSE;
		}
		if (dhdpcie_osinfo->oob_irq_enabled) {
			disable_irq(dhdpcie_osinfo->oob_irq_num);
			dhdpcie_osinfo->oob_irq_enabled = FALSE;
		}
		free_irq(dhdpcie_osinfo->oob_irq_num, bus);
	}
	dhdpcie_osinfo->oob_irq_registered = FALSE;
}
#endif /* BCMPCIE_OOB_HOST_WAKE */
