/* linux/drivers/usb/gadget/exynos_ss_udc.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * EXYNOS SuperSpeed USB 3.0 Device Controlle driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __EXYNOS_SS_UDC_H__
#define __EXYNOS_SS_UDC_H__

#define DMA_ADDR_INVALID (~((dma_addr_t)0))

/* Maximum packet size for different speeds */
#define EP0_LS_MPS	8
#define EP_LS_MPS	8

#define EP0_FS_MPS	64
#define EP_FS_MPS	64

#define EP0_HS_MPS	64
#define EP_HS_MPS	512

#define EP0_SS_MPS	512
#define EP_SS_MPS	1024

#define EXYNOS_USB3_EPS	9

/* Has to be multiple of four */
#define EXYNOS_USB3_EVENT_BUFF_WSIZE	256
#define EXYNOS_USB3_EVENT_BUFF_BSIZE	(EXYNOS_USB3_EVENT_BUFF_WSIZE << 2)

#define EXYNOS_USB3_CTRL_BUFF_SIZE	8
#define EXYNOS_USB3_EP0_BUFF_SIZE	512

#define EXYNOS_USB3_U1_DEV_EXIT_LAT	0
#define EXYNOS_USB3_U2_DEV_EXIT_LAT	0x20

/* Device registers */
#define EXYNOS_USB3_DCFG		0xC700
#define EXYNOS_USB3_DCFG_IgnoreStreamPP			(1 << 23)
#define EXYNOS_USB3_DCFG_LPMCap				(1 << 22)
#define EXYNOS_USB3_DCFG_NumP_MASK			(0x1f << 17)
#define EXYNOS_USB3_DCFG_NumP_SHIFT			17
#define EXYNOS_USB3_DCFG_NumP(_x)			((_x) << 17)
#define EXYNOS_USB3_DCFG_IntrNum_MASK			(0x1f << 12)
#define EXYNOS_USB3_DCFG_IntrNum_SHIFT			12
#define EXYNOS_USB3_DCFG_IntrNum(_x)			(0x1f << 12)
#define EXYNOS_USB3_DCFG_PerFrInt_MASK			(0x3 << 10)
#define EXYNOS_USB3_DCFG_PerFrInt_SHIFT			10
#define EXYNOS_USB3_DCFG_PerFrInt(_x)			((_x) << 10)
#define EXYNOS_USB3_DCFG_DevAddr_MASK			(0x7f << 3)
#define EXYNOS_USB3_DCFG_DevAddr_SHIFT			3
#define EXYNOS_USB3_DCFG_DevAddr(_x)			((_x) << 3)
#define EXYNOS_USB3_DCFG_DevSpd_MASK			(0x7 << 0)
#define EXYNOS_USB3_DCFG_DevSpd_SHIFT			0
#define EXYNOS_USB3_DCFG_DevSpd(_x)			((_x) << 0)

#define EXYNOS_USB3_DCTL		0xC704
#define EXYNOS_USB3_DCTL_Run_Stop			(1 << 31)
#define EXYNOS_USB3_DCTL_CSftRst			(1 << 30)
#define EXYNOS_USB3_DCTL_LSftRst			(1 << 29)
#define EXYNOS_USB3_DCTL_HIRD_Thres_MASK		(0x1f << 24)
#define EXYNOS_USB3_DCTL_HIRD_Thres_SHIFT		24
#define EXYNOS_USB3_DCTL_HIRD_Thres(_x)			((_x) << 24)
#define EXYNOS_USB3_DCTL_AppL1Res			(1 << 23)
#define EXYNOS_USB3_DCTL_TrgtULSt_MASK			(0xf << 17)
#define EXYNOS_USB3_DCTL_TrgtULSt_SHIFT			17
#define EXYNOS_USB3_DCTL_TrgtULSt(_x)			((_x) << 17)
#define EXYNOS_USB3_DCTL_InitU2Ena			(1 << 12)
#define EXYNOS_USB3_DCTL_AcceptU2Ena			(1 << 11)
#define EXYNOS_USB3_DCTL_InitU1Ena			(1 << 10)
#define EXYNOS_USB3_DCTL_AcceptU1Ena			(1 << 9)
#define EXYNOS_USB3_DCTL_ULStChngReq_MASK		(0xf << 5)
#define EXYNOS_USB3_DCTL_ULStChngReq_SHIFT		5
#define EXYNOS_USB3_DCTL_ULStChngReq(_x)		((_x) << 5)
#define EXYNOS_USB3_DCTL_TstCtl_MASK			(0xf << 1)
#define EXYNOS_USB3_DCTL_TstCtl_SHIFT			1
#define EXYNOS_USB3_DCTL_TstCtl(_x)			((_x) << 1)

#define EXYNOS_USB3_DEVTEN		0xC708
#define EXYNOS_USB3_DEVTEN_VndrDevTstRcvedEn		(1 << 12)
#define EXYNOS_USB3_DEVTEN_EvntOverflowEn		(1 << 11)
#define EXYNOS_USB3_DEVTEN_CmdCmpltEn			(1 << 10)
#define EXYNOS_USB3_DEVTEN_ErrticErrEn			(1 << 9)
#define EXYNOS_USB3_DEVTEN_SofEn			(1 << 7)
#define EXYNOS_USB3_DEVTEN_EOPFEn			(1 << 6)
#define EXYNOS_USB3_DEVTEN_WkUpEvtEn			(1 << 4)
#define EXYNOS_USB3_DEVTEN_ULStCngEn			(1 << 3)
#define EXYNOS_USB3_DEVTEN_ConnectDoneEn		(1 << 2)
#define EXYNOS_USB3_DEVTEN_USBRstEn			(1 << 1)
#define EXYNOS_USB3_DEVTEN_DisconnEvtEn			(1 << 0)

#define EXYNOS_USB3_DSTS		0xC70C
#define EXYNOS_USB3_DSTS_PwrUpReq			(1 << 24)
#define EXYNOS_USB3_DSTS_CoreIdle			(1 << 23)
#define EXYNOS_USB3_DSTS_DevCtrlHlt			(1 << 22)
#define EXYNOS_USB3_DSTS_USBLnkSt_MASK			(0xf << 18)
#define EXYNOS_USB3_DSTS_USBLnkSt_SHIFT			18
#define EXYNOS_USB3_DSTS_USBLnkSt(_x)			((_x) << 18)
#define EXYNOS_USB3_LnkSt_LPBK				0xb
#define EXYNOS_USB3_LnkSt_CMPLY				0xa
#define EXYNOS_USB3_LnkSt_HRESET			0x9
#define EXYNOS_USB3_LnkSt_RECOV				0x8
#define EXYNOS_USB3_LnkSt_POLL				0x7
#define EXYNOS_USB3_LnkSt_SS_INACT			0x6
#define EXYNOS_USB3_LnkSt_RX_DET			0x5
#define EXYNOS_USB3_LnkSt_SS_DIS			0x4
#define EXYNOS_USB3_LnkSt_U3				0x3
#define EXYNOS_USB3_LnkSt_U2				0x2
#define EXYNOS_USB3_LnkSt_U1				0x1
#define EXYNOS_USB3_LnkSt_U0				0x0
#define EXYNOS_USB3_DSTS_RxFIFOEmpty			(1 << 17)
#define EXYNOS_USB3_DSTS_SOFFN_MASK			(0x3fff << 3)
#define EXYNOS_USB3_DSTS_SOFFN_SHIFT			3
#define EXYNOS_USB3_DSTS_SOFFN(_x)			((_x) << 3)
#define EXYNOS_USB3_DSTS_ConnectSpd_MASK		(0x7 << 0)
#define EXYNOS_USB3_DSTS_ConnectSpd_SHIFT		0
#define EXYNOS_USB3_DSTS_ConnectSpd(_x)			((_x) << 0)

#define EXYNOS_USB3_DGCMDPAR		0xC710

#define EXYNOS_USB3_DGCMD		0xC714
#define EXYNOS_USB3_DGCMD_CmdStatus			(1 << 15)
#define EXYNOS_USB3_DGCMD_CmdAct			(1 << 10)
#define EXYNOS_USB3_DGCMD_CmdIOC			(1 << 8)
#define EXYNOS_USB3_DGCMD_CmdTyp_MASK			(0xff << 0)
#define EXYNOS_USB3_DGCMD_CmdTyp_SHIFT			0
#define EXYNOS_USB3_DGCMD_CmdTyp(_x)			((_x) << 0)
/* Device generic commands */
#define EXYNOS_USB3_DGCMD_CmdTyp_SetPerParams		0x2

#define EXYNOS_USB3_DALEPENA		0xC720

#define EXYNOS_USB3_DEPCMDPAR2(_a)	(0xC800 + ((_a) * 0x10))

#define EXYNOS_USB3_DEPCMDPAR1(_a)	(0xC804 + ((_a) * 0x10))
/* DEPCFG command parameter 1 */
#define EXYNOS_USB3_DEPCMDPAR1x_FIFO_based		(1 << 31)
#define EXYNOS_USB3_DEPCMDPAR1x_BULK_based		(1 << 30)
#define EXYNOS_USB3_DEPCMDPAR1x_EpNum_MASK		(0xf << 26)
#define EXYNOS_USB3_DEPCMDPAR1x_EpNum_SHIFT		26
#define EXYNOS_USB3_DEPCMDPAR1x_EpNum(_x)		((_x) << 26)
#define EXYNOS_USB3_DEPCMDPAR1x_EpDir			(1 << 25)
#define EXYNOS_USB3_DEPCMDPAR1x_StrmCap			(1 << 24)
#define EXYNOS_USB3_DEPCMDPAR1x_bInterval_m1_MASK	(0xff << 16)
#define EXYNOS_USB3_DEPCMDPAR1x_bInterval_m1_SHIFT	16
#define EXYNOS_USB3_DEPCMDPAR1x_bInterval_m1(_x)	((_x) << 16)
#define EXYNOS_USB3_DEPCMDPAR1x_StreamEvtEn		(1 << 13)
#define EXYNOS_USB3_DEPCMDPAR1x_RxTxfifoEvtEn		(1 << 11)
#define EXYNOS_USB3_DEPCMDPAR1x_XferNRdyEn		(1 << 10)
#define EXYNOS_USB3_DEPCMDPAR1x_XferInProgEn		(1 << 9)
#define EXYNOS_USB3_DEPCMDPAR1x_XferCmplEn		(1 << 8)
#define EXYNOS_USB3_DEPCMDPAR1x_IntrNum_MASK		(0x1f << 0)
#define EXYNOS_USB3_DEPCMDPAR1x_IntrNum_SHIFT		0
#define EXYNOS_USB3_DEPCMDPAR1x_IntrNum(_x)		((_x) << 0)

#define EXYNOS_USB3_DEPCMDPAR0(_a)	(0xC808 + ((_a) * 0x10))
/* DEPCFG command parameter 0 */
#define EXYNOS_USB3_DEPCMDPAR0x_IgnrSeqNum		(1 << 31)
#define EXYNOS_USB3_DEPCMDPAR0x_DataSeqNum_MASK		(0x1f << 26)
#define EXYNOS_USB3_DEPCMDPAR0x_DataSeqNum_SHIFT	26
#define EXYNOS_USB3_DEPCMDPAR0x_DataSeqNum(_x)		((_x) << 26)
#define EXYNOS_USB3_DEPCMDPAR0x_BrstSiz_MASK		(0xf << 22)
#define EXYNOS_USB3_DEPCMDPAR0x_BrstSiz_SHIFT		22
#define EXYNOS_USB3_DEPCMDPAR0x_BrstSiz(_x)		((_x) << 22)
#define EXYNOS_USB3_DEPCMDPAR0x_FIFONum_MASK		(0x1f << 17)
#define EXYNOS_USB3_DEPCMDPAR0x_FIFONum_SHIFT		17
#define EXYNOS_USB3_DEPCMDPAR0x_FIFONum(_x)		((_x) << 17)
#define EXYNOS_USB3_DEPCMDPAR0x_MPS_MASK		(0x7ff << 3)
#define EXYNOS_USB3_DEPCMDPAR0x_MPS_SHIFT		3
#define EXYNOS_USB3_DEPCMDPAR0x_MPS(_x)			((_x) << 3)
#define EXYNOS_USB3_DEPCMDPAR0x_EPType_MASK		(0x3 << 1)
#define EXYNOS_USB3_DEPCMDPAR0x_EPType_SHIFT		1
#define EXYNOS_USB3_DEPCMDPAR0x_EPType(_x)		((_x) << 1)
/* DEPXFERCFG command parameter 0 */
#define EXYNOS_USB3_DEPCMDPAR0x_NumXferRes_MASK		(0xff << 0)
#define EXYNOS_USB3_DEPCMDPAR0x_NumXferRes_SHIFT	0
#define EXYNOS_USB3_DEPCMDPAR0x_NumXferRes(_x)		((_x) << 0)

#define EXYNOS_USB3_DEPCMD(_a)		(0xC80C + ((_a) * 0x10))
#define EXYNOS_USB3_DEPCMDx_CommandParam_MASK		(0xffff << 16)
#define EXYNOS_USB3_DEPCMDx_CommandParam_SHIFT		16
#define EXYNOS_USB3_DEPCMDx_CommandParam(_x)		((_x) << 16)
#define EXYNOS_USB3_DEPCMDx_EventParam_MASK		(0xffff << 16)
#define EXYNOS_USB3_DEPCMDx_EventParam_SHIFT		16
#define EXYNOS_USB3_DEPCMDx_EventParam(_x)		((_x) << 16)
#define EXYNOS_USB3_DEPCMDx_XferRscIdx_LIMIT		0x7f
#define EXYNOS_USB3_DEPCMDx_CmdStatus_MASK		(0xf << 12)
#define EXYNOS_USB3_DEPCMDx_CmdStatus_SHIFT		12
#define EXYNOS_USB3_DEPCMDx_CmdStatus(_x)		((_x) << 12)
#define EXYNOS_USB3_DEPCMDx_HiPri_ForceRM		(1 << 11)
#define EXYNOS_USB3_DEPCMDx_CmdAct			(1 << 10)
#define EXYNOS_USB3_DEPCMDx_CmdIOC			(1 << 8)
#define EXYNOS_USB3_DEPCMDx_CmdTyp_MASK			(0xf << 0)
#define EXYNOS_USB3_DEPCMDx_CmdTyp_SHIFT		0
#define EXYNOS_USB3_DEPCMDx_CmdTyp(_x)			((_x) << 0)
/* Physical Endpoint commands */
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPSTARTCFG		0x9
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPENDXFER		0x8
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPUPDXFER		0x7
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPSTRTXFER		0x6
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPCSTALL		0x5
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPSSTALL		0x4
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPGETDSEQ		0x3
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPXFERCFG		0x2
#define EXYNOS_USB3_DEPCMDx_CmdTyp_DEPCFG		0x1

/* Transfer Request Block */
#define EXYNOS_USB3_TRB_TRBSTS_MASK			(0xf << 28)
#define EXYNOS_USB3_TRB_TRBSTS_SHIFT			28
#define EXYNOS_USB3_TRB_TRBSTS(_x)			((_x) << 28)
#define EXYNOS_USB3_TRB_PCM1_MASK			(0x3 << 24)
#define EXYNOS_USB3_TRB_PCM1_SHIFT			24
#define EXYNOS_USB3_TRB_PCM1(_x)			((_x) << 24)
#define EXYNOS_USB3_TRB_BUFSIZ_MASK			(0xffffff << 0)
#define EXYNOS_USB3_TRB_BUFSIZ_SHIFT			0
#define EXYNOS_USB3_TRB_BUFSIZ(_x)			((_x) << 0)
#define EXYNOS_USB3_TRB_StreamID_SOFNumber_MASK		(0xffff << 14)
#define EXYNOS_USB3_TRB_StreamID_SOFNumber_SHIFT	14
#define EXYNOS_USB3_TRB_StreamID_SOFNumber(_x)		((_x) << 14)
#define EXYNOS_USB3_TRB_IOC				(1 << 11)
#define EXYNOS_USB3_TRB_ISP_IMI				(1 << 10)
#define EXYNOS_USB3_TRB_TRBCTL_MASK			(0x3f << 4)
#define EXYNOS_USB3_TRB_TRBCTL_SHIFT			4
#define EXYNOS_USB3_TRB_TRBCTL(_x)			((_x) << 4)
#define EXYNOS_USB3_TRB_CSP				(1 << 3)
#define EXYNOS_USB3_TRB_CHN				(1 << 2)
#define EXYNOS_USB3_TRB_LST				(1 << 1)
#define EXYNOS_USB3_TRB_HWO				(1 << 0)
/*****************************************************************************/

#define call_gadget(_udc, _entry)	do {			\
	if ((_udc)->gadget.speed != USB_SPEED_UNKNOWN &&	\
	    (_udc)->driver && (_udc)->driver->_entry)		\
		(_udc)->driver->_entry(&(_udc)->gadget);	\
} while (0)

/**
 * States of EP0
 */
enum ctrl_ep_state {
	EP0_UNCONNECTED,
	EP0_SETUP_PHASE,
	EP0_DATA_PHASE,
	EP0_WAIT_NRDY,
	EP0_STATUS_PHASE_2,
	EP0_STATUS_PHASE_3,
	EP0_STALL,
};

/**
 * Types of TRB
 */
enum trb_control {
	NORMAL = 1,
	CONTROL_SETUP,
	CONTROL_STATUS_2,
	CONTROL_STATUS_3,
	CONTROL_DATA,
	ISOCHRONOUS_FIRST,
	ISOCHRONOUS,
	LINK_TRB,
};

/**
 * struct exynos_ss_udc_trb - transfer request block (TRB)
 * @buff_ptr_low: Buffer pointer low.
 * @buff_ptr_high: Buffer pointer high.
 * @param1: TRB parameter 1.
 * @param2: TRB parameter 2.
 */
struct exynos_ss_udc_trb {
	u32 buff_ptr_low;
	u32 buff_ptr_high;
	u32 param1;
	u32 param2;
};

/**
 * struct exynos_ss_udc_ep_command - endpoint command.
 * @queue: The list of commands for the endpoint.
 * @ep: physical endpoint number.
 * @param0: Command parameter 0.
 * @param1: Command parameter 1.
 * @param2: Command parameter 2.
 * @cmdtype: Command to issue.
 * @cmdflags: Command flags.
 */
struct exynos_ss_udc_ep_command {
	struct list_head	queue;

	int ep;
	u32 param0;
	u32 param1;
	u32 param2;
	u32 cmdtyp;
	u32 cmdflags;
};

/**
 * struct exynos_ss_udc_req - data transfer request
 * @req: The USB gadget request.
 * @queue: The list of requests for the endpoint this is queued for.
 * @mapped: DMA buffer for this request has been mapped via dma_map_single().
 */
struct exynos_ss_udc_req {
	struct usb_request	req;
	struct list_head	queue;
	unsigned char		mapped;
};

/**
 * struct exynos_ss_udc_ep - driver endpoint definition.
 * @ep: The gadget layer representation of the endpoint.
 * @queue: Queue of requests for this endpoint.
 * @cmd_queue: Queue of commands for this endpoint.
 * @parent: Reference back to the parent device structure.
 * @req: The current request that the endpoint is processing. This is
 *       used to indicate an request has been loaded onto the endpoint
 *       and has yet to be completed (maybe due to data move, or simply
 *	 awaiting an ack from the core all the data has been completed).
 * @lock: State lock to protect contents of endpoint.
 * @trb: Transfer Request Block.
 * @trb_dma: Transfer Request Block DMA address.
 * @tri: Transfer resource index.
 * @epnum: The USB endpoint number.
 * @type: The endpoint type.
 * @dir_in: Set to true if this endpoint is of the IN direction, which
 *	    means that it is sending data to the Host.
 * @halted: Set if the endpoint has been halted.
 * @enabled: Set to true if endpoint is enabled.
 * @wedged: Set if the endpoint has been wedged.
 * @not_ready: Set to true if a command for the endpoint hasn't completed
 *	       during timeout interval.
 * @name: The driver generated name for the endpoint.
 *
 * This is the driver's state for each registered enpoint, allowing it
 * to keep track of transactions that need doing. Each endpoint has a
 * lock to protect the state, to try and avoid using an overall lock
 * for the host controller as much as possible.
 */
struct exynos_ss_udc_ep {
	struct usb_ep			ep;
	struct list_head		queue;
	struct list_head		cmd_queue;
	struct exynos_ss_udc		*parent;
	struct exynos_ss_udc_req	*req;

	spinlock_t			lock;

	struct exynos_ss_udc_trb	*trb;
	dma_addr_t			trb_dma;
	u8				tri;

	unsigned char		epnum;
	unsigned int		type;
	unsigned int		dir_in:1;
	unsigned int		halted:1;
	unsigned int		enabled:1;
	unsigned int		wedged:1;
	unsigned int		not_ready:1;

	char			name[10];
};

/**
 * struct exynos_ss_udc - driver state.
 * @dev: The parent device supplied to the probe function
 * @driver: USB gadget driver
 * @plat: The platform specific configuration data.
 * @regs: The memory area mapped for accessing registers.
 * @irq: The IRQ number we are using.
 * @clk: The clock we are using.
 * @release: The core release number.
 * @event_buff: Event buffer.
 * @event_buff_dma: Event buffer DMA address.
 * @event_indx: Event buffer index.
 * @eps_enabled: Set if new configuration for physical endpoints > 1 started.
 * @ep0_state: State of EP0.
 * @ep0_three_stage: Set if control transfer has three stages.
 * @ep0_buff: Buffer for EP0 data.
 * @ep0_buff_dma: EP0 data buffer DMA address.
 * @ctrl_buff: Buffer for EP0 control requests.
 * @ctrl_buff_dma: EP0 control request buffer DMA address.
 * @ctrl_req: Request for EP0 control packets.
 * @gadget: Represents USB slave device.
 * @eps: The endpoints being supplied to the gadget framework
 */
struct exynos_ss_udc {
	struct device			*dev;
	struct usb_gadget_driver	*driver;
	struct exynos_ss_udc_plat	*plat;

	void __iomem		*regs;
	int			irq;
	struct clk		*clk;

	u16			release;

	u32			*event_buff;
	dma_addr_t		event_buff_dma;
	u32			event_indx;

	bool			eps_enabled;
	enum ctrl_ep_state	ep0_state;
	int			ep0_three_stage;

	u8			*ep0_buff;
	dma_addr_t		ep0_buff_dma;
	u8			*ctrl_buff;
	dma_addr_t		ctrl_buff_dma;
	struct usb_request	*ctrl_req;

	struct usb_gadget	gadget;
	struct exynos_ss_udc_ep	eps[EXYNOS_USB3_EPS];
};

#ifdef CONFIG_BATTERY_SAMSUNG
extern void samsung_cable_check_status(int flag);
#endif

/* conversion functions */
static inline struct exynos_ss_udc_req *our_req(struct usb_request *req)
{
	return container_of(req, struct exynos_ss_udc_req, req);
}

static inline struct exynos_ss_udc_ep *our_ep(struct usb_ep *ep)
{
	return container_of(ep, struct exynos_ss_udc_ep, ep);
}

static inline void __orr32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) & ~val, ptr);
}

static inline int get_phys_epnum(struct exynos_ss_udc_ep *udc_ep)
{
	return udc_ep->epnum * 2 + udc_ep->dir_in;
}

static inline int get_usb_epnum(int index)
{
	return index >> 1;
}

#endif /* __EXYNOS_SS_UDC_H__ */
