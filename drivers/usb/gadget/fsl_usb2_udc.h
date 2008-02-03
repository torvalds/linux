/*
 * Freescale USB device/endpoint management registers
 */
#ifndef __FSL_USB2_UDC_H
#define __FSL_USB2_UDC_H

/* ### define USB registers here
 */
#define USB_MAX_CTRL_PAYLOAD		64
#define USB_DR_SYS_OFFSET		0x400

 /* USB DR device mode registers (Little Endian) */
struct usb_dr_device {
	/* Capability register */
	u8 res1[256];
	u16 caplength;		/* Capability Register Length */
	u16 hciversion;		/* Host Controller Interface Version */
	u32 hcsparams;		/* Host Controller Structual Parameters */
	u32 hccparams;		/* Host Controller Capability Parameters */
	u8 res2[20];
	u32 dciversion;		/* Device Controller Interface Version */
	u32 dccparams;		/* Device Controller Capability Parameters */
	u8 res3[24];
	/* Operation register */
	u32 usbcmd;		/* USB Command Register */
	u32 usbsts;		/* USB Status Register */
	u32 usbintr;		/* USB Interrupt Enable Register */
	u32 frindex;		/* Frame Index Register */
	u8 res4[4];
	u32 deviceaddr;		/* Device Address */
	u32 endpointlistaddr;	/* Endpoint List Address Register */
	u8 res5[4];
	u32 burstsize;		/* Master Interface Data Burst Size Register */
	u32 txttfilltuning;	/* Transmit FIFO Tuning Controls Register */
	u8 res6[24];
	u32 configflag;		/* Configure Flag Register */
	u32 portsc1;		/* Port 1 Status and Control Register */
	u8 res7[28];
	u32 otgsc;		/* On-The-Go Status and Control */
	u32 usbmode;		/* USB Mode Register */
	u32 endptsetupstat;	/* Endpoint Setup Status Register */
	u32 endpointprime;	/* Endpoint Initialization Register */
	u32 endptflush;		/* Endpoint Flush Register */
	u32 endptstatus;	/* Endpoint Status Register */
	u32 endptcomplete;	/* Endpoint Complete Register */
	u32 endptctrl[6];	/* Endpoint Control Registers */
};

 /* USB DR host mode registers (Little Endian) */
struct usb_dr_host {
	/* Capability register */
	u8 res1[256];
	u16 caplength;		/* Capability Register Length */
	u16 hciversion;		/* Host Controller Interface Version */
	u32 hcsparams;		/* Host Controller Structual Parameters */
	u32 hccparams;		/* Host Controller Capability Parameters */
	u8 res2[20];
	u32 dciversion;		/* Device Controller Interface Version */
	u32 dccparams;		/* Device Controller Capability Parameters */
	u8 res3[24];
	/* Operation register */
	u32 usbcmd;		/* USB Command Register */
	u32 usbsts;		/* USB Status Register */
	u32 usbintr;		/* USB Interrupt Enable Register */
	u32 frindex;		/* Frame Index Register */
	u8 res4[4];
	u32 periodiclistbase;	/* Periodic Frame List Base Address Register */
	u32 asynclistaddr;	/* Current Asynchronous List Address Register */
	u8 res5[4];
	u32 burstsize;		/* Master Interface Data Burst Size Register */
	u32 txttfilltuning;	/* Transmit FIFO Tuning Controls Register */
	u8 res6[24];
	u32 configflag;		/* Configure Flag Register */
	u32 portsc1;		/* Port 1 Status and Control Register */
	u8 res7[28];
	u32 otgsc;		/* On-The-Go Status and Control */
	u32 usbmode;		/* USB Mode Register */
	u32 endptsetupstat;	/* Endpoint Setup Status Register */
	u32 endpointprime;	/* Endpoint Initialization Register */
	u32 endptflush;		/* Endpoint Flush Register */
	u32 endptstatus;	/* Endpoint Status Register */
	u32 endptcomplete;	/* Endpoint Complete Register */
	u32 endptctrl[6];	/* Endpoint Control Registers */
};

 /* non-EHCI USB system interface registers (Big Endian) */
struct usb_sys_interface {
	u32 snoop1;
	u32 snoop2;
	u32 age_cnt_thresh;	/* Age Count Threshold Register */
	u32 pri_ctrl;		/* Priority Control Register */
	u32 si_ctrl;		/* System Interface Control Register */
	u8 res[236];
	u32 control;		/* General Purpose Control Register */
};

/* ep0 transfer state */
#define WAIT_FOR_SETUP          0
#define DATA_STATE_XMIT         1
#define DATA_STATE_NEED_ZLP     2
#define WAIT_FOR_OUT_STATUS     3
#define DATA_STATE_RECV         4

/* Device Controller Capability Parameter register */
#define DCCPARAMS_DC				0x00000080
#define DCCPARAMS_DEN_MASK			0x0000001f

/* Frame Index Register Bit Masks */
#define	USB_FRINDEX_MASKS			0x3fff
/* USB CMD  Register Bit Masks */
#define  USB_CMD_RUN_STOP                     0x00000001
#define  USB_CMD_CTRL_RESET                   0x00000002
#define  USB_CMD_PERIODIC_SCHEDULE_EN         0x00000010
#define  USB_CMD_ASYNC_SCHEDULE_EN            0x00000020
#define  USB_CMD_INT_AA_DOORBELL              0x00000040
#define  USB_CMD_ASP                          0x00000300
#define  USB_CMD_ASYNC_SCH_PARK_EN            0x00000800
#define  USB_CMD_SUTW                         0x00002000
#define  USB_CMD_ATDTW                        0x00004000
#define  USB_CMD_ITC                          0x00FF0000

/* bit 15,3,2 are frame list size */
#define  USB_CMD_FRAME_SIZE_1024              0x00000000
#define  USB_CMD_FRAME_SIZE_512               0x00000004
#define  USB_CMD_FRAME_SIZE_256               0x00000008
#define  USB_CMD_FRAME_SIZE_128               0x0000000C
#define  USB_CMD_FRAME_SIZE_64                0x00008000
#define  USB_CMD_FRAME_SIZE_32                0x00008004
#define  USB_CMD_FRAME_SIZE_16                0x00008008
#define  USB_CMD_FRAME_SIZE_8                 0x0000800C

/* bit 9-8 are async schedule park mode count */
#define  USB_CMD_ASP_00                       0x00000000
#define  USB_CMD_ASP_01                       0x00000100
#define  USB_CMD_ASP_10                       0x00000200
#define  USB_CMD_ASP_11                       0x00000300
#define  USB_CMD_ASP_BIT_POS                  8

/* bit 23-16 are interrupt threshold control */
#define  USB_CMD_ITC_NO_THRESHOLD             0x00000000
#define  USB_CMD_ITC_1_MICRO_FRM              0x00010000
#define  USB_CMD_ITC_2_MICRO_FRM              0x00020000
#define  USB_CMD_ITC_4_MICRO_FRM              0x00040000
#define  USB_CMD_ITC_8_MICRO_FRM              0x00080000
#define  USB_CMD_ITC_16_MICRO_FRM             0x00100000
#define  USB_CMD_ITC_32_MICRO_FRM             0x00200000
#define  USB_CMD_ITC_64_MICRO_FRM             0x00400000
#define  USB_CMD_ITC_BIT_POS                  16

/* USB STS Register Bit Masks */
#define  USB_STS_INT                          0x00000001
#define  USB_STS_ERR                          0x00000002
#define  USB_STS_PORT_CHANGE                  0x00000004
#define  USB_STS_FRM_LST_ROLL                 0x00000008
#define  USB_STS_SYS_ERR                      0x00000010
#define  USB_STS_IAA                          0x00000020
#define  USB_STS_RESET                        0x00000040
#define  USB_STS_SOF                          0x00000080
#define  USB_STS_SUSPEND                      0x00000100
#define  USB_STS_HC_HALTED                    0x00001000
#define  USB_STS_RCL                          0x00002000
#define  USB_STS_PERIODIC_SCHEDULE            0x00004000
#define  USB_STS_ASYNC_SCHEDULE               0x00008000

/* USB INTR Register Bit Masks */
#define  USB_INTR_INT_EN                      0x00000001
#define  USB_INTR_ERR_INT_EN                  0x00000002
#define  USB_INTR_PTC_DETECT_EN               0x00000004
#define  USB_INTR_FRM_LST_ROLL_EN             0x00000008
#define  USB_INTR_SYS_ERR_EN                  0x00000010
#define  USB_INTR_ASYN_ADV_EN                 0x00000020
#define  USB_INTR_RESET_EN                    0x00000040
#define  USB_INTR_SOF_EN                      0x00000080
#define  USB_INTR_DEVICE_SUSPEND              0x00000100

/* Device Address bit masks */
#define  USB_DEVICE_ADDRESS_MASK              0xFE000000
#define  USB_DEVICE_ADDRESS_BIT_POS           25

/* endpoint list address bit masks */
#define USB_EP_LIST_ADDRESS_MASK              0xfffff800

/* PORTSCX  Register Bit Masks */
#define  PORTSCX_CURRENT_CONNECT_STATUS       0x00000001
#define  PORTSCX_CONNECT_STATUS_CHANGE        0x00000002
#define  PORTSCX_PORT_ENABLE                  0x00000004
#define  PORTSCX_PORT_EN_DIS_CHANGE           0x00000008
#define  PORTSCX_OVER_CURRENT_ACT             0x00000010
#define  PORTSCX_OVER_CURRENT_CHG             0x00000020
#define  PORTSCX_PORT_FORCE_RESUME            0x00000040
#define  PORTSCX_PORT_SUSPEND                 0x00000080
#define  PORTSCX_PORT_RESET                   0x00000100
#define  PORTSCX_LINE_STATUS_BITS             0x00000C00
#define  PORTSCX_PORT_POWER                   0x00001000
#define  PORTSCX_PORT_INDICTOR_CTRL           0x0000C000
#define  PORTSCX_PORT_TEST_CTRL               0x000F0000
#define  PORTSCX_WAKE_ON_CONNECT_EN           0x00100000
#define  PORTSCX_WAKE_ON_CONNECT_DIS          0x00200000
#define  PORTSCX_WAKE_ON_OVER_CURRENT         0x00400000
#define  PORTSCX_PHY_LOW_POWER_SPD            0x00800000
#define  PORTSCX_PORT_FORCE_FULL_SPEED        0x01000000
#define  PORTSCX_PORT_SPEED_MASK              0x0C000000
#define  PORTSCX_PORT_WIDTH                   0x10000000
#define  PORTSCX_PHY_TYPE_SEL                 0xC0000000

/* bit 11-10 are line status */
#define  PORTSCX_LINE_STATUS_SE0              0x00000000
#define  PORTSCX_LINE_STATUS_JSTATE           0x00000400
#define  PORTSCX_LINE_STATUS_KSTATE           0x00000800
#define  PORTSCX_LINE_STATUS_UNDEF            0x00000C00
#define  PORTSCX_LINE_STATUS_BIT_POS          10

/* bit 15-14 are port indicator control */
#define  PORTSCX_PIC_OFF                      0x00000000
#define  PORTSCX_PIC_AMBER                    0x00004000
#define  PORTSCX_PIC_GREEN                    0x00008000
#define  PORTSCX_PIC_UNDEF                    0x0000C000
#define  PORTSCX_PIC_BIT_POS                  14

/* bit 19-16 are port test control */
#define  PORTSCX_PTC_DISABLE                  0x00000000
#define  PORTSCX_PTC_JSTATE                   0x00010000
#define  PORTSCX_PTC_KSTATE                   0x00020000
#define  PORTSCX_PTC_SEQNAK                   0x00030000
#define  PORTSCX_PTC_PACKET                   0x00040000
#define  PORTSCX_PTC_FORCE_EN                 0x00050000
#define  PORTSCX_PTC_BIT_POS                  16

/* bit 27-26 are port speed */
#define  PORTSCX_PORT_SPEED_FULL              0x00000000
#define  PORTSCX_PORT_SPEED_LOW               0x04000000
#define  PORTSCX_PORT_SPEED_HIGH              0x08000000
#define  PORTSCX_PORT_SPEED_UNDEF             0x0C000000
#define  PORTSCX_SPEED_BIT_POS                26

/* bit 28 is parallel transceiver width for UTMI interface */
#define  PORTSCX_PTW                          0x10000000
#define  PORTSCX_PTW_8BIT                     0x00000000
#define  PORTSCX_PTW_16BIT                    0x10000000

/* bit 31-30 are port transceiver select */
#define  PORTSCX_PTS_UTMI                     0x00000000
#define  PORTSCX_PTS_ULPI                     0x80000000
#define  PORTSCX_PTS_FSLS                     0xC0000000
#define  PORTSCX_PTS_BIT_POS                  30

/* otgsc Register Bit Masks */
#define  OTGSC_CTRL_VUSB_DISCHARGE            0x00000001
#define  OTGSC_CTRL_VUSB_CHARGE               0x00000002
#define  OTGSC_CTRL_OTG_TERM                  0x00000008
#define  OTGSC_CTRL_DATA_PULSING              0x00000010
#define  OTGSC_STS_USB_ID                     0x00000100
#define  OTGSC_STS_A_VBUS_VALID               0x00000200
#define  OTGSC_STS_A_SESSION_VALID            0x00000400
#define  OTGSC_STS_B_SESSION_VALID            0x00000800
#define  OTGSC_STS_B_SESSION_END              0x00001000
#define  OTGSC_STS_1MS_TOGGLE                 0x00002000
#define  OTGSC_STS_DATA_PULSING               0x00004000
#define  OTGSC_INTSTS_USB_ID                  0x00010000
#define  OTGSC_INTSTS_A_VBUS_VALID            0x00020000
#define  OTGSC_INTSTS_A_SESSION_VALID         0x00040000
#define  OTGSC_INTSTS_B_SESSION_VALID         0x00080000
#define  OTGSC_INTSTS_B_SESSION_END           0x00100000
#define  OTGSC_INTSTS_1MS                     0x00200000
#define  OTGSC_INTSTS_DATA_PULSING            0x00400000
#define  OTGSC_INTR_USB_ID                    0x01000000
#define  OTGSC_INTR_A_VBUS_VALID              0x02000000
#define  OTGSC_INTR_A_SESSION_VALID           0x04000000
#define  OTGSC_INTR_B_SESSION_VALID           0x08000000
#define  OTGSC_INTR_B_SESSION_END             0x10000000
#define  OTGSC_INTR_1MS_TIMER                 0x20000000
#define  OTGSC_INTR_DATA_PULSING              0x40000000

/* USB MODE Register Bit Masks */
#define  USB_MODE_CTRL_MODE_IDLE              0x00000000
#define  USB_MODE_CTRL_MODE_DEVICE            0x00000002
#define  USB_MODE_CTRL_MODE_HOST              0x00000003
#define  USB_MODE_CTRL_MODE_RSV               0x00000001
#define  USB_MODE_SETUP_LOCK_OFF              0x00000008
#define  USB_MODE_STREAM_DISABLE              0x00000010
/* Endpoint Flush Register */
#define EPFLUSH_TX_OFFSET		      0x00010000
#define EPFLUSH_RX_OFFSET		      0x00000000

/* Endpoint Setup Status bit masks */
#define  EP_SETUP_STATUS_MASK                 0x0000003F
#define  EP_SETUP_STATUS_EP0		      0x00000001

/* ENDPOINTCTRLx  Register Bit Masks */
#define  EPCTRL_TX_ENABLE                     0x00800000
#define  EPCTRL_TX_DATA_TOGGLE_RST            0x00400000	/* Not EP0 */
#define  EPCTRL_TX_DATA_TOGGLE_INH            0x00200000	/* Not EP0 */
#define  EPCTRL_TX_TYPE                       0x000C0000
#define  EPCTRL_TX_DATA_SOURCE                0x00020000	/* Not EP0 */
#define  EPCTRL_TX_EP_STALL                   0x00010000
#define  EPCTRL_RX_ENABLE                     0x00000080
#define  EPCTRL_RX_DATA_TOGGLE_RST            0x00000040	/* Not EP0 */
#define  EPCTRL_RX_DATA_TOGGLE_INH            0x00000020	/* Not EP0 */
#define  EPCTRL_RX_TYPE                       0x0000000C
#define  EPCTRL_RX_DATA_SINK                  0x00000002	/* Not EP0 */
#define  EPCTRL_RX_EP_STALL                   0x00000001

/* bit 19-18 and 3-2 are endpoint type */
#define  EPCTRL_EP_TYPE_CONTROL               0
#define  EPCTRL_EP_TYPE_ISO                   1
#define  EPCTRL_EP_TYPE_BULK                  2
#define  EPCTRL_EP_TYPE_INTERRUPT             3
#define  EPCTRL_TX_EP_TYPE_SHIFT              18
#define  EPCTRL_RX_EP_TYPE_SHIFT              2

/* SNOOPn Register Bit Masks */
#define  SNOOP_ADDRESS_MASK                   0xFFFFF000
#define  SNOOP_SIZE_ZERO                      0x00	/* snooping disable */
#define  SNOOP_SIZE_4KB                       0x0B	/* 4KB snoop size */
#define  SNOOP_SIZE_8KB                       0x0C
#define  SNOOP_SIZE_16KB                      0x0D
#define  SNOOP_SIZE_32KB                      0x0E
#define  SNOOP_SIZE_64KB                      0x0F
#define  SNOOP_SIZE_128KB                     0x10
#define  SNOOP_SIZE_256KB                     0x11
#define  SNOOP_SIZE_512KB                     0x12
#define  SNOOP_SIZE_1MB                       0x13
#define  SNOOP_SIZE_2MB                       0x14
#define  SNOOP_SIZE_4MB                       0x15
#define  SNOOP_SIZE_8MB                       0x16
#define  SNOOP_SIZE_16MB                      0x17
#define  SNOOP_SIZE_32MB                      0x18
#define  SNOOP_SIZE_64MB                      0x19
#define  SNOOP_SIZE_128MB                     0x1A
#define  SNOOP_SIZE_256MB                     0x1B
#define  SNOOP_SIZE_512MB                     0x1C
#define  SNOOP_SIZE_1GB                       0x1D
#define  SNOOP_SIZE_2GB                       0x1E	/* 2GB snoop size */

/* pri_ctrl Register Bit Masks */
#define  PRI_CTRL_PRI_LVL1                    0x0000000C
#define  PRI_CTRL_PRI_LVL0                    0x00000003

/* si_ctrl Register Bit Masks */
#define  SI_CTRL_ERR_DISABLE                  0x00000010
#define  SI_CTRL_IDRC_DISABLE                 0x00000008
#define  SI_CTRL_RD_SAFE_EN                   0x00000004
#define  SI_CTRL_RD_PREFETCH_DISABLE          0x00000002
#define  SI_CTRL_RD_PREFEFETCH_VAL            0x00000001

/* control Register Bit Masks */
#define  USB_CTRL_IOENB                       0x00000004
#define  USB_CTRL_ULPI_INT0EN                 0x00000001

/* Endpoint Queue Head data struct
 * Rem: all the variables of qh are LittleEndian Mode
 * and NEXT_POINTER_MASK should operate on a LittleEndian, Phy Addr
 */
struct ep_queue_head {
	u32 max_pkt_length;	/* Mult(31-30) , Zlt(29) , Max Pkt len
				   and IOS(15) */
	u32 curr_dtd_ptr;	/* Current dTD Pointer(31-5) */
	u32 next_dtd_ptr;	/* Next dTD Pointer(31-5), T(0) */
	u32 size_ioc_int_sts;	/* Total bytes (30-16), IOC (15),
				   MultO(11-10), STS (7-0)  */
	u32 buff_ptr0;		/* Buffer pointer Page 0 (31-12) */
	u32 buff_ptr1;		/* Buffer pointer Page 1 (31-12) */
	u32 buff_ptr2;		/* Buffer pointer Page 2 (31-12) */
	u32 buff_ptr3;		/* Buffer pointer Page 3 (31-12) */
	u32 buff_ptr4;		/* Buffer pointer Page 4 (31-12) */
	u32 res1;
	u8 setup_buffer[8];	/* Setup data 8 bytes */
	u32 res2[4];
};

/* Endpoint Queue Head Bit Masks */
#define  EP_QUEUE_HEAD_MULT_POS               30
#define  EP_QUEUE_HEAD_ZLT_SEL                0x20000000
#define  EP_QUEUE_HEAD_MAX_PKT_LEN_POS        16
#define  EP_QUEUE_HEAD_MAX_PKT_LEN(ep_info)   (((ep_info)>>16)&0x07ff)
#define  EP_QUEUE_HEAD_IOS                    0x00008000
#define  EP_QUEUE_HEAD_NEXT_TERMINATE         0x00000001
#define  EP_QUEUE_HEAD_IOC                    0x00008000
#define  EP_QUEUE_HEAD_MULTO                  0x00000C00
#define  EP_QUEUE_HEAD_STATUS_HALT	      0x00000040
#define  EP_QUEUE_HEAD_STATUS_ACTIVE          0x00000080
#define  EP_QUEUE_CURRENT_OFFSET_MASK         0x00000FFF
#define  EP_QUEUE_HEAD_NEXT_POINTER_MASK      0xFFFFFFE0
#define  EP_QUEUE_FRINDEX_MASK                0x000007FF
#define  EP_MAX_LENGTH_TRANSFER               0x4000

/* Endpoint Transfer Descriptor data struct */
/* Rem: all the variables of td are LittleEndian Mode */
struct ep_td_struct {
	u32 next_td_ptr;	/* Next TD pointer(31-5), T(0) set
				   indicate invalid */
	u32 size_ioc_sts;	/* Total bytes (30-16), IOC (15),
				   MultO(11-10), STS (7-0)  */
	u32 buff_ptr0;		/* Buffer pointer Page 0 */
	u32 buff_ptr1;		/* Buffer pointer Page 1 */
	u32 buff_ptr2;		/* Buffer pointer Page 2 */
	u32 buff_ptr3;		/* Buffer pointer Page 3 */
	u32 buff_ptr4;		/* Buffer pointer Page 4 */
	u32 res;
	/* 32 bytes */
	dma_addr_t td_dma;	/* dma address for this td */
	/* virtual address of next td specified in next_td_ptr */
	struct ep_td_struct *next_td_virt;
};

/* Endpoint Transfer Descriptor bit Masks */
#define  DTD_NEXT_TERMINATE                   0x00000001
#define  DTD_IOC                              0x00008000
#define  DTD_STATUS_ACTIVE                    0x00000080
#define  DTD_STATUS_HALTED                    0x00000040
#define  DTD_STATUS_DATA_BUFF_ERR             0x00000020
#define  DTD_STATUS_TRANSACTION_ERR           0x00000008
#define  DTD_RESERVED_FIELDS                  0x80007300
#define  DTD_ADDR_MASK                        0xFFFFFFE0
#define  DTD_PACKET_SIZE                      0x7FFF0000
#define  DTD_LENGTH_BIT_POS                   16
#define  DTD_ERROR_MASK                       (DTD_STATUS_HALTED | \
                                               DTD_STATUS_DATA_BUFF_ERR | \
                                               DTD_STATUS_TRANSACTION_ERR)
/* Alignment requirements; must be a power of two */
#define DTD_ALIGNMENT				0x20
#define QH_ALIGNMENT				2048

/* Controller dma boundary */
#define UDC_DMA_BOUNDARY			0x1000

/* -----------------------------------------------------------------------*/
/* ##### enum data
*/
typedef enum {
	e_ULPI,
	e_UTMI_8BIT,
	e_UTMI_16BIT,
	e_SERIAL
} e_PhyInterface;

/*-------------------------------------------------------------------------*/

/* ### driver private data
 */
struct fsl_req {
	struct usb_request req;
	struct list_head queue;
	/* ep_queue() func will add
	   a request->queue into a udc_ep->queue 'd tail */
	struct fsl_ep *ep;
	unsigned mapped:1;

	struct ep_td_struct *head, *tail;	/* For dTD List
						   cpu endian Virtual addr */
	unsigned int dtd_count;
};

#define REQ_UNCOMPLETE			1

struct fsl_ep {
	struct usb_ep ep;
	struct list_head queue;
	struct fsl_udc *udc;
	struct ep_queue_head *qh;
	const struct usb_endpoint_descriptor *desc;
	struct usb_gadget *gadget;

	char name[14];
	unsigned stopped:1;
};

#define EP_DIR_IN	1
#define EP_DIR_OUT	0

struct fsl_udc {

	struct usb_gadget gadget;
	struct usb_gadget_driver *driver;
	struct fsl_ep *eps;
	unsigned int max_ep;
	unsigned int irq;

	struct usb_ctrlrequest local_setup_buff;
	spinlock_t lock;
	struct otg_transceiver *transceiver;
	unsigned softconnect:1;
	unsigned vbus_active:1;
	unsigned stopped:1;
	unsigned remote_wakeup:1;

	struct ep_queue_head *ep_qh;	/* Endpoints Queue-Head */
	struct fsl_req *status_req;	/* ep0 status request */
	struct dma_pool *td_pool;	/* dma pool for DTD */
	enum fsl_usb2_phy_modes phy_mode;

	size_t ep_qh_size;		/* size after alignment adjustment*/
	dma_addr_t ep_qh_dma;		/* dma address of QH */

	u32 max_pipes;		/* Device max pipes */
	u32 max_use_endpts;	/* Max endpointes to be used */
	u32 bus_reset;		/* Device is bus reseting */
	u32 resume_state;	/* USB state to resume */
	u32 usb_state;		/* USB current state */
	u32 usb_next_state;	/* USB next state */
	u32 ep0_state;		/* Endpoint zero state */
	u32 ep0_dir;		/* Endpoint zero direction: can be
				   USB_DIR_IN or USB_DIR_OUT */
	u32 usb_sof_count;	/* SOF count */
	u32 errors;		/* USB ERRORs count */
	u8 device_address;	/* Device USB address */

	struct completion *done;	/* to make sure release() is done */
};

/*-------------------------------------------------------------------------*/

#ifdef DEBUG
#define DBG(fmt, args...) 	printk(KERN_DEBUG "[%s]  " fmt "\n", \
				__FUNCTION__, ## args)
#else
#define DBG(fmt, args...)	do{}while(0)
#endif

#if 0
static void dump_msg(const char *label, const u8 * buf, unsigned int length)
{
	unsigned int start, num, i;
	char line[52], *p;

	if (length >= 512)
		return;
	DBG("%s, length %u:\n", label, length);
	start = 0;
	while (length > 0) {
		num = min(length, 16u);
		p = line;
		for (i = 0; i < num; ++i) {
			if (i == 8)
				*p++ = ' ';
			sprintf(p, " %02x", buf[i]);
			p += 3;
		}
		*p = 0;
		printk(KERN_DEBUG "%6x: %s\n", start, line);
		buf += num;
		start += num;
		length -= num;
	}
}
#endif

#ifdef VERBOSE
#define VDBG		DBG
#else
#define VDBG(stuff...)	do{}while(0)
#endif

#define ERR(stuff...)		pr_err("udc: " stuff)
#define WARN(stuff...)		pr_warning("udc: " stuff)
#define INFO(stuff...)		pr_info("udc: " stuff)

/*-------------------------------------------------------------------------*/

/* ### Add board specific defines here
 */

/*
 * ### pipe direction macro from device view
 */
#define USB_RECV	0	/* OUT EP */
#define USB_SEND	1	/* IN EP */

/*
 * ### internal used help routines.
 */
#define ep_index(EP)		((EP)->desc->bEndpointAddress&0xF)
#define ep_maxpacket(EP)	((EP)->ep.maxpacket)
#define ep_is_in(EP)	( (ep_index(EP) == 0) ? (EP->udc->ep0_dir == \
			USB_DIR_IN ):((EP)->desc->bEndpointAddress \
			& USB_DIR_IN)==USB_DIR_IN)
#define get_ep_by_pipe(udc, pipe)	((pipe == 1)? &udc->eps[0]: \
					&udc->eps[pipe])
#define get_pipe_by_windex(windex)	((windex & USB_ENDPOINT_NUMBER_MASK) \
					* 2 + ((windex & USB_DIR_IN) ? 1 : 0))
#define get_pipe_by_ep(EP)	(ep_index(EP) * 2 + ep_is_in(EP))

#endif
