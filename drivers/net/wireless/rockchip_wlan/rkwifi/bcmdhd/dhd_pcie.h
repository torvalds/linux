/*
 * Linux DHD Bus Module for PCIE
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef dhd_pcie_h
#define dhd_pcie_h

#include <bcmpcie.h>
#include <hnd_cons.h>
#include <dhd_linux.h>
#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
#ifdef CONFIG_PCI_MSM
#include <linux/msm_pcie.h>
#else
#include <mach/msm_pcie.h>
#endif /* CONFIG_PCI_MSM */
#endif /* CONFIG_ARCH_MSM */
#ifdef CONFIG_ARCH_EXYNOS
#ifndef SUPPORT_EXYNOS7420
#include <linux/exynos-pci-noti.h>
extern int exynos_pcie_register_event(struct exynos_pcie_register_event *reg);
extern int exynos_pcie_deregister_event(struct exynos_pcie_register_event *reg);
#endif /* !SUPPORT_EXYNOS7420 */
#endif /* CONFIG_ARCH_EXYNOS */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

#ifdef DHD_PCIE_RUNTIMEPM
#include <linux/mutex.h>
#include <linux/wait.h>
#endif /* DHD_PCIE_RUNTIMEPM */

/* defines */
#define PCIE_SHARED_VERSION		PCIE_SHARED_VERSION_7

#define PCMSGBUF_HDRLEN 0
#define DONGLE_REG_MAP_SIZE (32 * 1024)
#define DONGLE_TCM_MAP_SIZE (4096 * 1024)
#define DONGLE_MIN_MEMSIZE (128 *1024)
#ifdef DHD_DEBUG
#define DHD_PCIE_SUCCESS 0
#define DHD_PCIE_FAILURE 1
#endif /* DHD_DEBUG */
#define	REMAP_ENAB(bus)			((bus)->remap)
#define	REMAP_ISADDR(bus, a)		(((a) >= ((bus)->orig_ramsize)) && ((a) < ((bus)->ramsize)))

#ifdef SUPPORT_LINKDOWN_RECOVERY
#ifdef CONFIG_ARCH_MSM
#define struct_pcie_notify		struct msm_pcie_notify
#define struct_pcie_register_event	struct msm_pcie_register_event
#endif /* CONFIG_ARCH_MSM */
#ifdef CONFIG_ARCH_EXYNOS
#ifndef SUPPORT_EXYNOS7420
#define struct_pcie_notify		struct exynos_pcie_notify
#define struct_pcie_register_event	struct exynos_pcie_register_event
#endif /* !SUPPORT_EXYNOS7420 */
#endif /* CONFIG_ARCH_EXYNOS */
#endif /* SUPPORT_LINKDOWN_RECOVERY */

#define MAX_DHD_TX_FLOWS	320

/* user defined data structures */
/* Device console log buffer state */
#define CONSOLE_LINE_MAX	192u
#define CONSOLE_BUFFER_MAX	(8 * 1024)

#ifdef IDLE_TX_FLOW_MGMT
#define IDLE_FLOW_LIST_TIMEOUT 5000
#define IDLE_FLOW_RING_TIMEOUT 5000
#endif /* IDLE_TX_FLOW_MGMT */

#ifdef DEVICE_TX_STUCK_DETECT
#define DEVICE_TX_STUCK_CKECK_TIMEOUT	1000 /* 1 sec */
#define DEVICE_TX_STUCK_TIMEOUT		10000 /* 10 secs */
#define DEVICE_TX_STUCK_WARN_DURATION (DEVICE_TX_STUCK_TIMEOUT / DEVICE_TX_STUCK_CKECK_TIMEOUT)
#define DEVICE_TX_STUCK_DURATION (DEVICE_TX_STUCK_WARN_DURATION * 2)
#endif /* DEVICE_TX_STUCK_DETECT */

/* implicit DMA for h2d wr and d2h rd indice from Host memory to TCM */
#define IDMA_ENAB(dhd)		((dhd) && (dhd)->idma_enable)
#define IDMA_ACTIVE(dhd)	((dhd) && ((dhd)->idma_enable) && ((dhd)->idma_inited))

#define IDMA_CAPABLE(bus)	(((bus)->sih->buscorerev == 19) || ((bus)->sih->buscorerev >= 23))

/* IFRM (Implicit Flow Ring Manager enable and inited */
#define IFRM_ENAB(dhd)		((dhd) && (dhd)->ifrm_enable)
#define IFRM_ACTIVE(dhd)	((dhd) && ((dhd)->ifrm_enable) && ((dhd)->ifrm_inited))

/* DAR registers use for h2d doorbell */
#define DAR_ENAB(dhd)		((dhd) && (dhd)->dar_enable)
#define DAR_ACTIVE(dhd)		((dhd) && ((dhd)->dar_enable) && ((dhd)->dar_inited))

/* DAR WAR for revs < 64 */
#define DAR_PWRREQ(bus)		(((bus)->_dar_war) && DAR_ACTIVE((bus)->dhd))

/* PCIE CTO Prevention and Recovery */
#define PCIECTO_ENAB(bus)	((bus)->cto_enable)

/* Implicit DMA index usage :
 * Index 0 for h2d write index transfer
 * Index 1 for d2h read index transfer
 */
#define IDMA_IDX0 0
#define IDMA_IDX1 1
#define IDMA_IDX2 2
#define IDMA_IDX3 3
#define DMA_TYPE_SHIFT	4
#define DMA_TYPE_IDMA	1

#define DHDPCIE_CONFIG_HDR_SIZE 16
#define DHDPCIE_CONFIG_CHECK_DELAY_MS 10 /* 10ms */
#define DHDPCIE_CONFIG_CHECK_RETRY_COUNT 20
#define DHDPCIE_DONGLE_PWR_TOGGLE_DELAY 1000 /* 1ms in units of us */
#define DHDPCIE_PM_D3_DELAY 200000 /* 200ms in units of us */
#define DHDPCIE_PM_D2_DELAY 200 /* 200us */

typedef struct dhd_console {
	 uint		count;	/* Poll interval msec counter */
	 uint		log_addr;		 /* Log struct address (fixed) */
	 hnd_log_t	 log;			 /* Log struct (host copy) */
	 uint		 bufsize;		 /* Size of log buffer */
	 uint8		 *buf;			 /* Log buffer (host copy) */
	 uint		 last;			 /* Last buffer read index */
} dhd_console_t;

typedef struct ring_sh_info {
	uint32 ring_mem_addr;
	uint32 ring_state_w;
	uint32 ring_state_r;
	pcie_hwa_db_index_t ring_hwa_db_idx;		/* HWA DB index value per ring */
} ring_sh_info_t;
#define MAX_DS_TRACE_SIZE	50
#ifdef DHD_MMIO_TRACE
#define MAX_MMIO_TRACE_SIZE	256
/* Minimum of 250us should be elapsed to add new entry */
#define MIN_MMIO_TRACE_TIME 250
#define DHD_RING_IDX 0x00FF0000
typedef struct _dhd_mmio_trace_t {
	uint64  timestamp;
	uint32	addr;
	uint32	value;
	bool	set;
} dhd_mmio_trace_t;
#endif /* defined(DHD_MMIO_TRACE) */
typedef struct _dhd_ds_trace_t {
	uint64  timestamp;
	bool	d2h;
	uint32	dsval;
#ifdef PCIE_INB_DW
	enum dhd_bus_ds_state inbstate;
#endif /* PCIE_INB_DW */
} dhd_ds_trace_t;

#define DEVICE_WAKE_NONE	0
#define DEVICE_WAKE_OOB		1
#define DEVICE_WAKE_INB		2

#define INBAND_DW_ENAB(bus)		((bus)->dw_option == DEVICE_WAKE_INB)
#define OOB_DW_ENAB(bus)		((bus)->dw_option == DEVICE_WAKE_OOB)
#define NO_DW_ENAB(bus)			((bus)->dw_option == DEVICE_WAKE_NONE)

#define PCIE_PWR_REQ_RELOAD_WAR_ENAB(buscorerev) \
	((buscorerev == 66) || (buscorerev == 67) || (buscorerev == 68) || \
	(buscorerev == 70) || (buscorerev == 72))

#define PCIE_FASTLPO_ENABLED(buscorerev) \
	((buscorerev == 66) || (buscorerev == 67) || (buscorerev == 68) || \
	(buscorerev == 70) || (buscorerev == 72))

/*
 * HW JIRA - CRWLPCIEGEN2-672
 * Producer Index Feature which is used by F1 gets reset on F0 FLR
 * fixed in REV68
 */
#define PCIE_ENUM_RESET_WAR_ENAB(buscorerev) \
	((buscorerev == 66) || (buscorerev == 67))

struct dhd_bus;

struct dhd_pcie_rev {
	uint8	fw_rev;
	void (*handle_mb_data)(struct dhd_bus *);
};

typedef struct dhdpcie_config_save
{
	uint32 header[DHDPCIE_CONFIG_HDR_SIZE];
	/* pmcsr save */
	uint32 pmcsr;
	/* express save */
	uint32 exp_dev_ctrl_stat;
	uint32 exp_link_ctrl_stat;
	uint32 exp_dev_ctrl_stat2;
	uint32 exp_link_ctrl_stat2;
	/* msi save */
	uint32 msi_cap;
	uint32 msi_addr0;
	uint32 msi_addr1;
	uint32 msi_data;
	/* l1pm save */
	uint32 l1pm0;
	uint32 l1pm1;
	/* ltr save */
	uint32 ltr;
	/* aer save */
	uint32 aer_caps_ctrl; /* 0x18 */
	uint32 aer_severity;  /* 0x0C */
	uint32 aer_umask;     /* 0x08 */
	uint32 aer_cmask;     /* 0x14 */
	uint32 aer_root_cmd;  /* 0x2c */
	/* BAR0 and BAR1 windows */
	uint32 bar0_win;
	uint32 bar1_win;
} dhdpcie_config_save_t;

/* The level of bus communication with the dongle */
enum dhd_bus_low_power_state {
	DHD_BUS_NO_LOW_POWER_STATE,	/* Not in low power state */
	DHD_BUS_D3_INFORM_SENT,		/* D3 INFORM sent */
	DHD_BUS_D3_ACK_RECIEVED,	/* D3 ACK recieved */
};

#ifdef DHD_FLOW_RING_STATUS_TRACE
#define FRS_TRACE_SIZE 32 /* frs - flow_ring_status */
typedef struct _dhd_flow_ring_status_trace_t {
	uint64  timestamp;
	uint16 h2d_ctrl_post_drd;
	uint16 h2d_ctrl_post_dwr;
	uint16 d2h_ctrl_cpln_drd;
	uint16 d2h_ctrl_cpln_dwr;
	uint16 h2d_rx_post_drd;
	uint16 h2d_rx_post_dwr;
	uint16 d2h_rx_cpln_drd;
	uint16 d2h_rx_cpln_dwr;
	uint16 d2h_tx_cpln_drd;
	uint16 d2h_tx_cpln_dwr;
	uint16 h2d_info_post_drd;
	uint16 h2d_info_post_dwr;
	uint16 d2h_info_cpln_drd;
	uint16 d2h_info_cpln_dwr;
	uint16 d2h_ring_edl_drd;
	uint16 d2h_ring_edl_dwr;
} dhd_frs_trace_t;
#endif /* DHD_FLOW_RING_STATUS_TRACE */

/** Instantiated once for each hardware (dongle) instance that this DHD manages */
typedef struct dhd_bus {
	dhd_pub_t	*dhd;	/**< pointer to per hardware (dongle) unique instance */
#if !defined(NDIS)
	struct pci_dev  *rc_dev;	/* pci RC device handle */
	struct pci_dev  *dev;		/* pci device handle */
#endif /* !defined(NDIS) */
#ifdef DHD_EFI
	void *pcie_dev;
#endif
	dll_t		flowring_active_list; /* constructed list of tx flowring queues */
#ifdef IDLE_TX_FLOW_MGMT
	uint64		active_list_last_process_ts;
						/* stores the timestamp of active list processing */
#endif /* IDLE_TX_FLOW_MGMT */

#ifdef DEVICE_TX_STUCK_DETECT
	/* Flag to enable/disable device tx stuck monitor by DHD IOVAR dev_tx_stuck_monitor */
	uint32 dev_tx_stuck_monitor;
	/* Stores the timestamp (msec) of the last device Tx stuck check */
	uint32	device_tx_stuck_check;
#endif /* DEVICE_TX_STUCK_DETECT */

	si_t		*sih;			/* Handle for SI calls */
	char		*vars;			/* Variables (from CIS and/or other) */
	uint		varsz;			/* Size of variables buffer */
	uint32		sbaddr;			/* Current SB window pointer (-1, invalid) */
	sbpcieregs_t	*reg;			/* Registers for PCIE core */

	uint		armrev;			/* CPU core revision */
	uint		coreid;			/* CPU core id */
	uint		ramrev;			/* SOCRAM core revision */
	uint32		ramsize;		/* Size of RAM in SOCRAM (bytes) */
	uint32		orig_ramsize;		/* Size of RAM in SOCRAM (bytes) */
	uint32		srmemsize;		/* Size of SRMEM */

	uint32		bus;			/* gSPI or SDIO bus */
	uint32		bus_num;		/* bus number */
	uint32		slot_num;		/* slot ID */
	uint32		intstatus;		/* Intstatus bits (events) pending */
	bool		dpc_sched;		/* Indicates DPC schedule (intrpt rcvd) */
	bool		fcstate;		/* State of dongle flow-control */

	uint16		cl_devid;		/* cached devid for dhdsdio_probe_attach() */
	char		*fw_path;		/* module_param: path to firmware image */
	char		*nv_path;		/* module_param: path to nvram vars file */
#ifdef CACHE_FW_IMAGES
	int			processed_nvram_params_len;	/* Modified len of NVRAM info */
#endif

#ifdef BCM_ROUTER_DHD
	char		*nvram_params;		/* user specified nvram params. */
	int			nvram_params_len;
#endif /* BCM_ROUTER_DHD */

	struct pktq	txq;			/* Queue length used for flow-control */

	bool		intr;			/* Use interrupts */
	bool		poll;			/* Use polling */
	bool		ipend;			/* Device interrupt is pending */
	bool		intdis;			/* Interrupts disabled by isr */
	uint		intrcount;		/* Count of device interrupt callbacks */
	uint		lastintrs;		/* Count as of last watchdog timer */

	dhd_console_t	console;		/* Console output polling support */
	uint		console_addr;		/* Console address from shared struct */

	bool		alp_only;		/* Don't use HT clock (ALP only) */

	bool		remap;		/* Contiguous 1MB RAM: 512K socram + 512K devram
					 * Available with socram rev 16
					 * Remap region not DMA-able
					 */
	uint32		resetinstr;
	uint32		dongle_ram_base;
	uint32		next_tlv;	/* Holds location of next available TLV */
	ulong		shared_addr;
	pciedev_shared_t	*pcie_sh;
	uint32		dma_rxoffset;
	volatile char	*regs;		/* pci device memory va */
	volatile char	*tcm;		/* pci device memory va */
	uint32		bar1_size;	/* pci device memory size */
	uint32		curr_bar1_win;	/* current PCIEBar1Window setting */
	osl_t		*osh;
	uint32		nvram_csm;	/* Nvram checksum */
#ifdef BCMINTERNAL
	bool            msi_sim;
	uchar           *msi_sim_addr;
	dmaaddr_t       msi_sim_phys;
	dhd_dma_buf_t   hostfw_buf;	/* Host offload firmware buffer */
	uint32          hostfw_base;	/* FW assumed base of host offload mem */
	uint32          bp_base;	/* adjusted bp base of host offload mem */
#endif /* BCMINTERNAL */
	uint16		pollrate;
	uint16  polltick;

	volatile uint32  *pcie_mb_intr_addr;
	volatile uint32  *pcie_mb_intr_2_addr;
	void    *pcie_mb_intr_osh;
	bool	sleep_allowed;

	wake_counts_t	wake_counts;

	/* version 3 shared struct related info start */
	ring_sh_info_t	ring_sh[BCMPCIE_COMMON_MSGRINGS + MAX_DHD_TX_FLOWS];

	uint8	h2d_ring_count;
	uint8	d2h_ring_count;
	uint32  ringmem_ptr;
	uint32  ring_state_ptr;

	uint32 d2h_dma_scratch_buffer_mem_addr;

	uint32 h2d_mb_data_ptr_addr;
	uint32 d2h_mb_data_ptr_addr;
	/* version 3 shared struct related info end */

	uint32 def_intmask;
	uint32 d2h_mb_mask;
	uint32 pcie_mailbox_mask;
	uint32 pcie_mailbox_int;
	bool	ltrsleep_on_unload;
	uint	wait_for_d3_ack;
	uint16	max_tx_flowrings;
	uint16	max_submission_rings;
	uint16	max_completion_rings;
	uint16	max_cmn_rings;
	uint32	rw_index_sz;
	uint32	hwa_db_index_sz;
	bool	db1_for_mb;

	dhd_timeout_t doorbell_timer;
	bool	device_wake_state;
#ifdef PCIE_OOB
	bool	oob_enabled;
#endif /* PCIE_OOB */
	bool	irq_registered;
	bool	d2h_intr_method;
	bool	d2h_intr_control;
#ifdef SUPPORT_LINKDOWN_RECOVERY
#if defined(CONFIG_ARCH_MSM) || (defined(CONFIG_ARCH_EXYNOS) && \
	!defined(SUPPORT_EXYNOS7420))
#ifdef CONFIG_ARCH_MSM
	uint8 no_cfg_restore;
#endif /* CONFIG_ARCH_MSM */
	struct_pcie_register_event pcie_event;
#endif /* CONFIG_ARCH_MSM || CONFIG_ARCH_EXYNOS && !SUPPORT_EXYNOS7420  */
	bool read_shm_fail;
#endif /* SUPPORT_LINKDOWN_RECOVERY */
	int32 idletime;                 /* Control for activity timeout */
	bool rpm_enabled;
#ifdef DHD_PCIE_RUNTIMEPM
	int32 idlecount;                /* Activity timeout counter */
	int32 bus_wake;                 /* For wake up the bus */
	bool runtime_resume_done;       /* For check runtime suspend end */
	struct mutex pm_lock;            /* Synchronize for system PM & runtime PM */
	wait_queue_head_t rpm_queue;    /* wait-queue for bus wake up */
#endif /* DHD_PCIE_RUNTIMEPM */
	uint32 d3_inform_cnt;
	uint32 d0_inform_cnt;
	uint32 d0_inform_in_use_cnt;
	uint8 force_suspend;
	uint8 is_linkdown;
	uint8 no_bus_init;
#ifdef IDLE_TX_FLOW_MGMT
	bool enable_idle_flowring_mgmt;
#endif /* IDLE_TX_FLOW_MGMT */
	struct	dhd_pcie_rev api;
	bool use_mailbox;
	bool    use_d0_inform;
	void	*bus_lp_state_lock;
	void	*pwr_req_lock;
	bool	dongle_in_deepsleep;
	void	*dongle_ds_lock;
	bool	bar1_switch_enab;
	void	*bar1_switch_lock;
	void *backplane_access_lock;
	enum dhd_bus_low_power_state bus_low_power_state;
#ifdef DHD_FLOW_RING_STATUS_TRACE
	dhd_frs_trace_t frs_isr_trace[FRS_TRACE_SIZE]; /* frs - flow_ring_status */
	dhd_frs_trace_t frs_dpc_trace[FRS_TRACE_SIZE]; /* frs - flow_ring_status */
	uint32	frs_isr_count;
	uint32	frs_dpc_count;
#endif /* DHD_FLOW_RING_STATUS_TRACE */
#ifdef DHD_MMIO_TRACE
	dhd_mmio_trace_t   mmio_trace[MAX_MMIO_TRACE_SIZE];
	uint32	mmio_trace_count;
#endif /* defined(DHD_MMIO_TRACE) */
	dhd_ds_trace_t   ds_trace[MAX_DS_TRACE_SIZE];
	uint32	ds_trace_count;
	uint32  hostready_count; /* Number of hostready issued */
#if defined(PCIE_OOB) || defined (BCMPCIE_OOB_HOST_WAKE)
	bool	oob_presuspend;
#endif /* PCIE_OOB || BCMPCIE_OOB_HOST_WAKE */
	dhdpcie_config_save_t saved_config;
	ulong resume_intr_enable_count;
	ulong dpc_intr_enable_count;
	ulong isr_intr_disable_count;
	ulong suspend_intr_disable_count;
	ulong dpc_return_busdown_count;
	ulong non_ours_irq_count;
#ifdef BCMPCIE_OOB_HOST_WAKE
	ulong oob_intr_count;
	ulong oob_intr_enable_count;
	ulong oob_intr_disable_count;
	uint64 last_oob_irq_isr_time;
	uint64 last_oob_irq_thr_time;
	uint64 last_oob_irq_enable_time;
	uint64 last_oob_irq_disable_time;
#endif /* BCMPCIE_OOB_HOST_WAKE */
	uint64 isr_entry_time;
	uint64 isr_exit_time;
	uint64 isr_sched_dpc_time;
	uint64 rpm_sched_dpc_time;
	uint64 dpc_entry_time;
	uint64 dpc_exit_time;
	uint64 resched_dpc_time;
	uint64 last_d3_inform_time;
	uint64 last_process_ctrlbuf_time;
	uint64 last_process_flowring_time;
	uint64 last_process_txcpl_time;
	uint64 last_process_rxcpl_time;
	uint64 last_process_infocpl_time;
	uint64 last_process_edl_time;
	uint64 last_suspend_start_time;
	uint64 last_suspend_end_time;
	uint64 last_resume_start_time;
	uint64 last_resume_end_time;
	uint64 last_non_ours_irq_time;
	bool  hwa_enabled;
	bool  idma_enabled;
	bool  ifrm_enabled;
	bool  dar_enabled;
	uint32 dmaxfer_complete;
	uint8	dw_option;
#ifdef PCIE_INB_DW
	bool	inb_enabled;
	uint32	ds_exit_timeout;
	uint32	host_sleep_exit_timeout;
	uint	wait_for_ds_exit;
	uint32	inband_dw_assert_cnt; /* # of inband device_wake assert */
	uint32	inband_dw_deassert_cnt; /* # of inband device_wake deassert */
	uint32	inband_ds_exit_host_cnt; /* # of DS-EXIT , host initiated */
	uint32	inband_ds_exit_device_cnt; /* # of DS-EXIT , device initiated */
	uint32	inband_ds_exit_to_cnt; /* # of DS-EXIT timeout */
	uint32	inband_host_sleep_exit_to_cnt; /* # of Host_Sleep exit timeout */
	void	*inb_lock;	/* Lock to serialize in band device wake activity */
	/* # of contexts in the host which currently want a FW transaction */
	uint32  host_active_cnt;
	bool	skip_ds_ack; /* Skip DS-ACK during suspend in progress */
#endif /* PCIE_INB_DW */
#if defined(PCIE_OOB) || defined(PCIE_INB_DW)
	bool  ds_enabled;
#endif
#ifdef DHD_PCIE_RUNTIMEPM
	bool chk_pm;	/* To avoid counting of wake up from Runtime PM */
#endif /* DHD_PCIE_RUNTIMEPM */
#if defined(PCIE_INB_DW)
	bool calc_ds_exit_latency;
	bool deep_sleep; /* Indicates deep_sleep set or unset by the DHD IOVAR deep_sleep */
	uint64 ds_exit_latency;
	uint64 ds_exit_ts1;
	uint64 ds_exit_ts2;
#endif /* PCIE_INB_DW */
	bool _dar_war;
#ifdef GDB_PROXY
	/* True if firmware loaded and backplane accessible */
	bool gdb_proxy_access_enabled;
	/* ID set by last "gdb_proxy_probe" iovar */
	uint32 gdb_proxy_last_id;
	/* True if firmware was started in bootloader mode */
	bool gdb_proxy_bootloader_mode;
#endif /* GDB_PROXY */
	uint8  dma_chan;

	bool    cto_enable;     /* enable PCIE CTO Prevention and recovery */
	uint32  cto_threshold;  /* PCIE CTO timeout threshold */
	bool	cto_triggered;	/* CTO is triggered */
	bool intr_enabled; /* ready to receive interrupts from dongle */
	int	pwr_req_ref;
	bool flr_force_fail; /* user intends to simulate flr force fail */

	/* Information used to compose the memory map and to write the memory map,
	 * FW, and FW signature to dongle RAM.
	 * This information is used by the bootloader.
	 */
	uint32 ramtop_addr;		/* Dongle address of unused space at top of RAM */
	uint32 fw_download_addr;	/* Dongle address of FW download */
	uint32 fw_download_len;		/* Length in bytes of FW download */
	uint32 fwsig_download_addr;	/* Dongle address of FW signature download */
	uint32 fwsig_download_len;	/* Length in bytes of FW signature download */
	uint32 fwstat_download_addr;	/* Dongle address of FWS status download */
	uint32 fwstat_download_len;	/* Length in bytes of FWS status download */
	uint32 fw_memmap_download_addr;	/* Dongle address of FWS memory-info download */
	uint32 fw_memmap_download_len;	/* Length in bytes of FWS memory-info download */

	char fwsig_filename[DHD_FILENAME_MAX];		/* Name of FW signature file */
	char bootloader_filename[DHD_FILENAME_MAX];	/* Name of bootloader image file */
	uint32 bootloader_addr;		/* Dongle address of bootloader download */
	bool force_bt_quiesce; /* send bt_quiesce command to BT driver. */
	bool rc_ep_aspm_cap; /* RC and EP ASPM capable */
	bool rc_ep_l1ss_cap; /* EC and EP L1SS capable */
#if defined(DHD_H2D_LOG_TIME_SYNC)
	ulong dhd_rte_time_sync_count; /* OSL_SYSUPTIME_US() */
#endif /* DHD_H2D_LOG_TIME_SYNC */
#ifdef D2H_MINIDUMP
	bool d2h_minidump; /* This flag will be set if Host and FW handshake to collect minidump */
	bool d2h_minidump_override; /* Force disable minidump through dhd IOVAR */
#endif /* D2H_MINIDUMP */
#ifdef BCMSLTGT
	int xtalfreq;		/* Xtal frequency used for htclkratio calculation */
	uint32 ilp_tick;	/* ILP ticks per second read from pmutimer */
	uint32 xtal_ratio;	/* xtal ticks per 4 ILP ticks read from pmu_xtalfreq */
#endif /* BCMSLTGT */
#ifdef BT_OVER_PCIE
	/* whether the chip is in BT over PCIE mode or not */
	bool btop_mode;
#endif /* BT_OVER_PCIE */
	uint16 hp2p_txcpl_max_items;
	uint16 hp2p_rxcpl_max_items;
	/* PCIE coherent status */
	uint32 coherent_state;
	uint32 inb_dw_deassert_cnt;
	uint64 arm_oor_time;
	uint64 rd_shared_pass_time;
	uint32 hwa_mem_base;
	uint32 hwa_mem_size;
} dhd_bus_t;

#ifdef DHD_MSI_SUPPORT
extern uint enable_msi;
#endif /* DHD_MSI_SUPPORT */

enum {
	PCIE_INTX = 0,
	PCIE_MSI = 1
};

enum {
	PCIE_D2H_INTMASK_CTRL = 0,
	PCIE_HOST_IRQ_CTRL = 1
};

static INLINE bool
__dhd_check_bus_in_lps(dhd_bus_t *bus)
{
	bool ret = (bus->bus_low_power_state == DHD_BUS_D3_INFORM_SENT) ||
		(bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED);
	return ret;
}

static INLINE bool
dhd_check_bus_in_lps(dhd_bus_t *bus)
{
	unsigned long flags_bus;
	bool ret;
	DHD_BUS_LP_STATE_LOCK(bus->bus_lp_state_lock, flags_bus);
	ret = __dhd_check_bus_in_lps(bus);
	DHD_BUS_LP_STATE_UNLOCK(bus->bus_lp_state_lock, flags_bus);
	return ret;
}

static INLINE bool
__dhd_check_bus_lps_d3_acked(dhd_bus_t *bus)
{
	bool ret = (bus->bus_low_power_state == DHD_BUS_D3_ACK_RECIEVED);
	return ret;
}

static INLINE bool
dhd_check_bus_lps_d3_acked(dhd_bus_t *bus)
{
	unsigned long flags_bus;
	bool ret;
	DHD_BUS_LP_STATE_LOCK(bus->bus_lp_state_lock, flags_bus);
	ret = __dhd_check_bus_lps_d3_acked(bus);
	DHD_BUS_LP_STATE_UNLOCK(bus->bus_lp_state_lock, flags_bus);
	return ret;
}

static INLINE void
__dhd_set_bus_not_in_lps(dhd_bus_t *bus)
{
	bus->bus_low_power_state = DHD_BUS_NO_LOW_POWER_STATE;
	return;
}

static INLINE void
dhd_set_bus_not_in_lps(dhd_bus_t *bus)
{
	unsigned long flags_bus;
	DHD_BUS_LP_STATE_LOCK(bus->bus_lp_state_lock, flags_bus);
	__dhd_set_bus_not_in_lps(bus);
	DHD_BUS_LP_STATE_UNLOCK(bus->bus_lp_state_lock, flags_bus);
	return;
}

static INLINE void
__dhd_set_bus_lps_d3_informed(dhd_bus_t *bus)
{
	bus->bus_low_power_state = DHD_BUS_D3_INFORM_SENT;
	return;
}

static INLINE void
dhd_set_bus_lps_d3_informed(dhd_bus_t *bus)
{
	unsigned long flags_bus;
	DHD_BUS_LP_STATE_LOCK(bus->bus_lp_state_lock, flags_bus);
	__dhd_set_bus_lps_d3_informed(bus);
	DHD_BUS_LP_STATE_UNLOCK(bus->bus_lp_state_lock, flags_bus);
	return;
}

static INLINE void
__dhd_set_bus_lps_d3_acked(dhd_bus_t *bus)
{
	bus->bus_low_power_state = DHD_BUS_D3_ACK_RECIEVED;
	return;
}

static INLINE void
dhd_set_bus_lps_d3_acked(dhd_bus_t *bus)
{
	unsigned long flags_bus;
	DHD_BUS_LP_STATE_LOCK(bus->bus_lp_state_lock, flags_bus);
	__dhd_set_bus_lps_d3_acked(bus);
	DHD_BUS_LP_STATE_UNLOCK(bus->bus_lp_state_lock, flags_bus);
	return;
}

/* check routines */
#define DHD_CHK_BUS_IN_LPS(bus)			dhd_check_bus_in_lps(bus)
#define __DHD_CHK_BUS_IN_LPS(bus)		__dhd_check_bus_in_lps(bus)

#define DHD_CHK_BUS_NOT_IN_LPS(bus)		!(DHD_CHK_BUS_IN_LPS(bus))
#define __DHD_CHK_BUS_NOT_IN_LPS(bus)		!(__DHD_CHK_BUS_IN_LPS(bus))

#define DHD_CHK_BUS_LPS_D3_INFORMED(bus)	DHD_CHK_BUS_IN_LPS(bus)
#define __DHD_CHK_BUS_LPS_D3_INFORMED(bus)	__DHD_CHK_BUS_IN_LPS(bus)

#define DHD_CHK_BUS_LPS_D3_ACKED(bus)		dhd_check_bus_lps_d3_acked(bus)
#define __DHD_CHK_BUS_LPS_D3_ACKED(bus)		__dhd_check_bus_lps_d3_acked(bus)

/* set routines */
#define DHD_SET_BUS_NOT_IN_LPS(bus)		dhd_set_bus_not_in_lps(bus)
#define __DHD_SET_BUS_NOT_IN_LPS(bus)		__dhd_set_bus_not_in_lps(bus)

#define DHD_SET_BUS_LPS_D3_INFORMED(bus)	dhd_set_bus_lps_d3_informed(bus)
#define __DHD_SET_BUS_LPS_D3_INFORMED(bus)	__dhd_set_bus_lps_d3_informed(bus)

#define DHD_SET_BUS_LPS_D3_ACKED(bus)		dhd_set_bus_lps_d3_acked(bus)
#define __DHD_SET_BUS_LPS_D3_ACKED(bus)		__dhd_set_bus_lps_d3_acked(bus)

/* function declarations */

extern uint32* dhdpcie_bus_reg_map(osl_t *osh, ulong addr, int size);
extern int dhdpcie_bus_register(void);
extern void dhdpcie_bus_unregister(void);
extern bool dhdpcie_chipmatch(uint16 vendor, uint16 device);

extern int dhdpcie_bus_attach(osl_t *osh, dhd_bus_t **bus_ptr,
	volatile char *regs, volatile char *tcm, void *pci_dev, wifi_adapter_info_t *adapter);
extern uint32 dhdpcie_bus_cfg_read_dword(struct dhd_bus *bus, uint32 addr, uint32 size);
extern void dhdpcie_bus_cfg_write_dword(struct dhd_bus *bus, uint32 addr, uint32 size, uint32 data);
extern void dhdpcie_bus_intr_enable(struct dhd_bus *bus);
extern void dhdpcie_bus_intr_disable(struct dhd_bus *bus);
extern int dhpcie_bus_mask_interrupt(dhd_bus_t *bus);
extern void dhdpcie_bus_release(struct dhd_bus *bus);
extern int32 dhdpcie_bus_isr(struct dhd_bus *bus);
extern void dhdpcie_free_irq(dhd_bus_t *bus);
extern void dhdpcie_bus_ringbell_fast(struct dhd_bus *bus, uint32 value);
extern void dhdpcie_bus_ringbell_2_fast(struct dhd_bus *bus, uint32 value, bool devwake);
extern void dhdpcie_dongle_reset(dhd_bus_t *bus);
extern int dhd_bus_cfg_sprom_ctrl_bp_reset(struct dhd_bus *bus);
extern int dhd_bus_cfg_ss_ctrl_bp_reset(struct dhd_bus *bus);
#ifdef DHD_PCIE_NATIVE_RUNTIMEPM
extern int dhdpcie_bus_suspend(struct  dhd_bus *bus, bool state, bool byint);
#else
extern int dhdpcie_bus_suspend(struct  dhd_bus *bus, bool state);
#endif /* DHD_PCIE_NATIVE_RUNTIMEPM */
extern int dhdpcie_pci_suspend_resume(struct  dhd_bus *bus, bool state);
extern uint32 dhdpcie_force_alp(struct dhd_bus *bus, bool enable);
extern uint32 dhdpcie_set_l1_entry_time(struct dhd_bus *bus, int force_l1_entry_time);
extern bool dhdpcie_tcm_valid(dhd_bus_t *bus);
extern void dhdpcie_pme_active(osl_t *osh, bool enable);
extern bool dhdpcie_pme_cap(osl_t *osh);
extern uint32 dhdpcie_lcreg(osl_t *osh, uint32 mask, uint32 val);
extern void dhdpcie_set_pmu_min_res_mask(struct dhd_bus *bus, uint min_res_mask);
extern uint8 dhdpcie_clkreq(osl_t *osh, uint32 mask, uint32 val);
extern int dhdpcie_disable_irq(dhd_bus_t *bus);
extern int dhdpcie_disable_irq_nosync(dhd_bus_t *bus);
extern int dhdpcie_enable_irq(dhd_bus_t *bus);

extern void dhd_bus_dump_dar_registers(struct dhd_bus *bus);

#if defined(linux) || defined(LINUX)
extern uint32 dhdpcie_rc_config_read(dhd_bus_t *bus, uint offset);
extern uint32 dhdpcie_rc_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext,
		bool is_write, uint32 writeval);
extern uint32 dhdpcie_ep_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext,
		bool is_write, uint32 writeval);
extern uint32 dhd_debug_get_rc_linkcap(dhd_bus_t *bus);
#else
static INLINE uint32 dhdpcie_rc_config_read(dhd_bus_t *bus, uint offset) { return 0;}
static INLINE uint32 dhdpcie_rc_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext,
		bool is_write, uint32 writeval) { return -1;}
static INLINE uint32 dhdpcie_ep_access_cap(dhd_bus_t *bus, int cap, uint offset, bool is_ext,
		bool is_write, uint32 writeval) { return -1;}
static INLINE uint32 dhd_debug_get_rc_linkcap(dhd_bus_t *bus) { return -1;}
#endif
#if defined(linux) || defined(LINUX)
extern int dhdpcie_start_host_dev(dhd_bus_t *bus);
extern int dhdpcie_stop_host_dev(dhd_bus_t *bus);
extern int dhdpcie_disable_device(dhd_bus_t *bus);
extern int dhdpcie_alloc_resource(dhd_bus_t *bus);
extern void dhdpcie_free_resource(dhd_bus_t *bus);
extern void dhdpcie_dump_resource(dhd_bus_t *bus);
extern int dhdpcie_bus_request_irq(struct dhd_bus *bus);
void dhdpcie_os_setbar1win(dhd_bus_t *bus, uint32 addr);
void dhdpcie_os_wtcm8(dhd_bus_t *bus, ulong offset, uint8 data);
uint8 dhdpcie_os_rtcm8(dhd_bus_t *bus, ulong offset);
void dhdpcie_os_wtcm16(dhd_bus_t *bus, ulong offset, uint16 data);
uint16 dhdpcie_os_rtcm16(dhd_bus_t *bus, ulong offset);
void dhdpcie_os_wtcm32(dhd_bus_t *bus, ulong offset, uint32 data);
uint32 dhdpcie_os_rtcm32(dhd_bus_t *bus, ulong offset);
#ifdef DHD_SUPPORT_64BIT
void dhdpcie_os_wtcm64(dhd_bus_t *bus, ulong offset, uint64 data);
uint64 dhdpcie_os_rtcm64(dhd_bus_t *bus, ulong offset);
#endif
#endif /* LINUX || linux */

#if defined(linux) || defined(LINUX) || defined(DHD_EFI)
extern int dhdpcie_enable_device(dhd_bus_t *bus);
#endif

#ifdef BCMPCIE_OOB_HOST_WAKE
extern int dhdpcie_oob_intr_register(dhd_bus_t *bus);
extern void dhdpcie_oob_intr_unregister(dhd_bus_t *bus);
extern void dhdpcie_oob_intr_set(dhd_bus_t *bus, bool enable);
extern int dhdpcie_get_oob_irq_num(struct dhd_bus *bus);
extern int dhdpcie_get_oob_irq_status(struct dhd_bus *bus);
extern int dhdpcie_get_oob_irq_level(void);
#endif /* BCMPCIE_OOB_HOST_WAKE */
#ifdef PCIE_OOB
extern void dhd_oob_set_bt_reg_on(struct dhd_bus *bus, bool val);
extern int dhd_oob_get_bt_reg_on(struct dhd_bus *bus);
extern void dhdpcie_oob_init(dhd_bus_t *bus);
extern int dhd_os_oob_set_device_wake(struct dhd_bus *bus, bool val);
extern void dhd_os_ib_set_device_wake(struct dhd_bus *bus, bool val);
#endif /* PCIE_OOB */
#if defined(PCIE_OOB) || defined(PCIE_INB_DW)
extern void dhd_bus_doorbell_timeout_reset(struct dhd_bus *bus);
#endif /* defined(PCIE_OOB) || defined(PCIE_INB_DW) */

#if defined(linux) || defined(LINUX)
/* XXX: SWWLAN-82173 Making PCIe RC D3cold by force during system PM
 * exynos_pcie_pm_suspend : RC goes to suspend status & assert PERST
 * exynos_pcie_pm_resume : de-assert PERST & RC goes to resume status
 */
#if defined(CONFIG_ARCH_EXYNOS)
#define EXYNOS_PCIE_VENDOR_ID 0x144d
#if defined(CONFIG_MACH_UNIVERSAL7420) || defined(CONFIG_SOC_EXYNOS7420)
#define EXYNOS_PCIE_DEVICE_ID 0xa575
#define EXYNOS_PCIE_CH_NUM 1
#elif defined(CONFIG_SOC_EXYNOS8890)
#define EXYNOS_PCIE_DEVICE_ID 0xa544
#define EXYNOS_PCIE_CH_NUM 0
#elif defined(CONFIG_SOC_EXYNOS8895) || defined(CONFIG_SOC_EXYNOS9810) || \
	defined(CONFIG_SOC_EXYNOS9820) || defined(CONFIG_SOC_EXYNOS9830) || \
	defined(CONFIG_SOC_EXYNOS2100) || defined(CONFIG_SOC_EXYNOS1000) || \
	defined(CONFIG_SOC_GS101)
#define EXYNOS_PCIE_DEVICE_ID 0xecec
#define EXYNOS_PCIE_CH_NUM 0
#else
#error "Not supported platform"
#endif /* CONFIG_SOC_EXYNOSXXXX & CONFIG_MACH_UNIVERSALXXXX */
extern void exynos_pcie_pm_suspend(int ch_num);
extern void exynos_pcie_pm_resume(int ch_num);
#endif /* CONFIG_ARCH_EXYNOS */

#if defined(CONFIG_ARCH_MSM)
#define MSM_PCIE_VENDOR_ID 0x17cb
#if defined(CONFIG_ARCH_APQ8084)
#define MSM_PCIE_DEVICE_ID 0x0101
#elif defined(CONFIG_ARCH_MSM8994)
#define MSM_PCIE_DEVICE_ID 0x0300
#elif defined(CONFIG_ARCH_MSM8996)
#define MSM_PCIE_DEVICE_ID 0x0104
#elif defined(CONFIG_ARCH_MSM8998)
#define MSM_PCIE_DEVICE_ID 0x0105
#elif defined(CONFIG_ARCH_SDM845) || defined(CONFIG_ARCH_SM8150) || \
	defined(CONFIG_ARCH_KONA) || defined(CONFIG_ARCH_LAHAINA)
#define MSM_PCIE_DEVICE_ID 0x0106
#else
#error "Not supported platform"
#endif
#endif /* CONFIG_ARCH_MSM */

#if defined(CONFIG_X86)
#define X86_PCIE_VENDOR_ID 0x8086
#define X86_PCIE_DEVICE_ID 0x9c1a
#endif /* CONFIG_X86 */

#if defined(CONFIG_ARCH_TEGRA)
#define TEGRA_PCIE_VENDOR_ID 0x14e4
#define TEGRA_PCIE_DEVICE_ID 0x4347
#endif /* CONFIG_ARCH_TEGRA */

#if defined(BOARD_HIKEY)
#define HIKEY_PCIE_VENDOR_ID 0x19e5
#define HIKEY_PCIE_DEVICE_ID 0x3660
#endif /* BOARD_HIKEY */

#define DUMMY_PCIE_VENDOR_ID 0xffff
#define DUMMY_PCIE_DEVICE_ID 0xffff

#if defined(CONFIG_ARCH_EXYNOS)
#define PCIE_RC_VENDOR_ID EXYNOS_PCIE_VENDOR_ID
#define PCIE_RC_DEVICE_ID EXYNOS_PCIE_DEVICE_ID
#elif defined(CONFIG_ARCH_MSM)
#define PCIE_RC_VENDOR_ID MSM_PCIE_VENDOR_ID
#define PCIE_RC_DEVICE_ID MSM_PCIE_DEVICE_ID
#elif defined(CONFIG_X86)
#define PCIE_RC_VENDOR_ID X86_PCIE_VENDOR_ID
#define PCIE_RC_DEVICE_ID X86_PCIE_DEVICE_ID
#elif defined(CONFIG_ARCH_TEGRA)
#define PCIE_RC_VENDOR_ID TEGRA_PCIE_VENDOR_ID
#define PCIE_RC_DEVICE_ID TEGRA_PCIE_DEVICE_ID
#elif defined(BOARD_HIKEY)
#define PCIE_RC_VENDOR_ID HIKEY_PCIE_VENDOR_ID
#define PCIE_RC_DEVICE_ID HIKEY_PCIE_DEVICE_ID
#else
/* Use dummy vendor and device IDs */
#define PCIE_RC_VENDOR_ID DUMMY_PCIE_VENDOR_ID
#define PCIE_RC_DEVICE_ID DUMMY_PCIE_DEVICE_ID
#endif /* CONFIG_ARCH_EXYNOS */
#endif /* linux || LINUX */

#define DHD_REGULAR_RING    0
#define DHD_HP2P_RING    1

#ifdef CONFIG_ARCH_TEGRA
extern int tegra_pcie_pm_suspend(void);
extern int tegra_pcie_pm_resume(void);
#endif /* CONFIG_ARCH_TEGRA */

extern int dhd_buzzz_dump_dngl(dhd_bus_t *bus);
#ifdef IDLE_TX_FLOW_MGMT
extern int dhd_bus_flow_ring_resume_request(struct dhd_bus *bus, void *arg);
extern void dhd_bus_flow_ring_resume_response(struct dhd_bus *bus, uint16 flowid, int32 status);
extern int dhd_bus_flow_ring_suspend_request(struct dhd_bus *bus, void *arg);
extern void dhd_bus_flow_ring_suspend_response(struct dhd_bus *bus, uint16 flowid, uint32 status);
extern void dhd_flow_ring_move_to_active_list_head(struct dhd_bus *bus,
	flow_ring_node_t *flow_ring_node);
extern void dhd_flow_ring_add_to_active_list(struct dhd_bus *bus,
	flow_ring_node_t *flow_ring_node);
extern void dhd_flow_ring_delete_from_active_list(struct dhd_bus *bus,
	flow_ring_node_t *flow_ring_node);
extern void __dhd_flow_ring_delete_from_active_list(struct dhd_bus *bus,
	flow_ring_node_t *flow_ring_node);
#endif /* IDLE_TX_FLOW_MGMT */

extern int dhdpcie_send_mb_data(dhd_bus_t *bus, uint32 h2d_mb_data);

#ifdef DHD_WAKE_STATUS
int bcmpcie_get_total_wake(struct dhd_bus *bus);
int bcmpcie_set_get_wake(struct dhd_bus *bus, int flag);
#endif /* DHD_WAKE_STATUS */
#ifdef DHD_MMIO_TRACE
extern void dhd_dump_bus_mmio_trace(dhd_bus_t *bus, struct bcmstrbuf *strbuf);
#endif /* defined(DHD_MMIO_TRACE) */
extern void dhd_dump_bus_ds_trace(dhd_bus_t *bus, struct bcmstrbuf *strbuf);
extern bool dhdpcie_bus_get_pcie_hostready_supported(dhd_bus_t *bus);
extern void dhd_bus_hostready(struct  dhd_bus *bus);
#ifdef PCIE_OOB
extern bool dhdpcie_bus_get_pcie_oob_dw_supported(dhd_bus_t *bus);
#endif /* PCIE_OOB */
#ifdef PCIE_INB_DW
extern bool dhdpcie_bus_get_pcie_inband_dw_supported(dhd_bus_t *bus);
extern void dhdpcie_bus_set_pcie_inband_dw_state(dhd_bus_t *bus,
	enum dhd_bus_ds_state state);
extern enum dhd_bus_ds_state dhdpcie_bus_get_pcie_inband_dw_state(dhd_bus_t *bus);
extern const char * dhd_convert_inb_state_names(enum dhd_bus_ds_state inbstate);
extern const char * dhd_convert_dsval(uint32 val, bool d2h);
extern int dhd_bus_inb_set_device_wake(struct dhd_bus *bus, bool val);
extern void dhd_bus_inb_ack_pending_ds_req(dhd_bus_t *bus);
#endif /* PCIE_INB_DW */
extern void dhdpcie_bus_enab_pcie_dw(dhd_bus_t *bus, uint8 dw_option);
#if defined(LINUX) || defined(linux)
extern int dhdpcie_irq_disabled(struct dhd_bus *bus);
extern int dhdpcie_set_master_and_d0_pwrstate(struct dhd_bus *bus);
#else
static INLINE bool dhdpcie_irq_disabled(struct dhd_bus *bus) { return BCME_ERROR;}
static INLINE int dhdpcie_set_master_and_d0_pwrstate(struct dhd_bus *bus)
{ return BCME_ERROR;}
#endif /* defined(LINUX) || defined(linux) */

#ifdef DHD_EFI
extern bool dhdpcie_is_arm_halted(struct dhd_bus *bus);
extern int dhd_os_wifi_platform_set_power(uint32 value);
extern void dhdpcie_dongle_pwr_toggle(dhd_bus_t *bus);
void dhdpcie_dongle_flr_or_pwr_toggle(dhd_bus_t *bus);
int dhd_control_signal(dhd_bus_t *bus, char *arg, int len, int set);
extern int dhd_wifi_properties(struct dhd_bus *bus, char *arg, int len);
extern int dhd_otp_dump(dhd_bus_t *bus, char *arg, int len);
extern int dhdpcie_deinit_phase1(dhd_bus_t *bus);
int dhdpcie_disable_intr_poll(dhd_bus_t *bus);
int dhdpcie_enable_intr_poll(dhd_bus_t *bus);
#ifdef BT_OVER_PCIE
int dhd_btop_test(dhd_bus_t *bus, char *arg, int len);
#endif /* BT_OVER_PCIE */
#else
static INLINE bool dhdpcie_is_arm_halted(struct dhd_bus *bus) {return TRUE;}
static INLINE int dhd_os_wifi_platform_set_power(uint32 value) {return BCME_OK; }
static INLINE void
dhdpcie_dongle_flr_or_pwr_toggle(dhd_bus_t *bus)
{ return; }
#endif /* DHD_EFI */

int dhdpcie_config_check(dhd_bus_t *bus);
int dhdpcie_config_restore(dhd_bus_t *bus, bool restore_pmcsr);
int dhdpcie_config_save(dhd_bus_t *bus);
int dhdpcie_set_pwr_state(dhd_bus_t *bus, uint state);

extern bool dhdpcie_bus_get_pcie_hwa_supported(dhd_bus_t *bus);
extern bool dhdpcie_bus_get_pcie_idma_supported(dhd_bus_t *bus);
extern bool dhdpcie_bus_get_pcie_ifrm_supported(dhd_bus_t *bus);
extern bool dhdpcie_bus_get_pcie_dar_supported(dhd_bus_t *bus);
extern bool dhdpcie_bus_get_hp2p_supported(dhd_bus_t *bus);

static INLINE uint32
dhd_pcie_config_read(dhd_bus_t *bus, uint offset, uint size)
{
	/* For 4375 or prior chips to 4375 */
	if (bus->sih && bus->sih->buscorerev <= 64) {
		OSL_DELAY(100);
	}
	return OSL_PCI_READ_CONFIG(bus->osh, offset, size);
}

static INLINE uint32
dhd_pcie_corereg_read(si_t *sih, uint val)
{
	/* For 4375 or prior chips to 4375 */
	if (sih->buscorerev <= 64) {
		OSL_DELAY(100);
	}
	si_corereg(sih, sih->buscoreidx, OFFSETOF(sbpcieregs_t, configaddr), ~0, val);
	return si_corereg(sih, sih->buscoreidx, OFFSETOF(sbpcieregs_t, configdata), 0, 0);
}

extern int dhdpcie_get_fwpath_otp(dhd_bus_t *bus, char *fw_path, char *nv_path,
		char *clm_path, char *txcap_path);

extern int dhd_pcie_debug_info_dump(dhd_pub_t *dhd);
extern void dhd_pcie_intr_count_dump(dhd_pub_t *dhd);
extern void dhdpcie_bus_clear_intstatus(dhd_bus_t *bus);
#ifdef DHD_HP2P
extern uint16 dhd_bus_get_hp2p_ring_max_size(dhd_bus_t *bus, bool tx);
#endif

#if defined(DHD_EFI)
extern wifi_properties_t *dhd_get_props(dhd_bus_t *bus);
#endif

#if defined(DHD_EFI) || defined(NDIS)
extern int dhd_get_platform(dhd_pub_t* dhd, char *progname);
extern bool dhdpcie_is_chip_supported(uint32 chipid, int *idx);
extern bool dhdpcie_is_sflash_chip(uint32 chipid);
#endif

extern int dhd_get_pcie_linkspeed(dhd_pub_t *dhd);
extern void dhdpcie_bar1_window_switch_enab(dhd_bus_t *bus);

#ifdef PCIE_INB_DW
extern void dhdpcie_set_dongle_deepsleep(dhd_bus_t *bus, bool val);
extern void dhd_init_dongle_ds_lock(dhd_bus_t *bus);
extern void dhd_deinit_dongle_ds_lock(dhd_bus_t *bus);
#endif /* PCIE_INB_DW */

#endif /* dhd_pcie_h */
