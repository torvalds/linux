/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TI VIP capture driver
 *
 * Copyright (C) 2025 Texas Instruments Incorpated - http://www.ti.com/
 * David Griego, <dagriego@biglakesoftware.com>
 * Dale Farnsworth, <dale@farnsworth.org>
 * Yemike Abhilash Chandra, <y-abhilashchandra@ti.com>
 */

#ifndef __TI_VIP_H
#define __TI_VIP_H

#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-memops.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-async.h>

#include "vpdma.h"
#include "vpdma_priv.h"
#include "sc.h"
#include "csc.h"

#define VIP_INSTANCE1	1
#define VIP_INSTANCE2	2
#define VIP_INSTANCE3	3

#define VIP_SLICE1	0
#define VIP_SLICE2	1
#define VIP_NUM_SLICES	2

/*
 * Additional client identifiers used for VPDMA configuration descriptors
 */
#define VIP_SLICE1_CFD_SC_CLIENT	7
#define VIP_SLICE2_CFD_SC_CLIENT	8

#define VIP_PORTA	0
#define VIP_PORTB	1
#define VIP_NUM_PORTS	2

#define VIP_MAX_PLANES	2
#define	VIP_LUMA	0
#define VIP_CHROMA	1

#define VIP_CAP_STREAMS_PER_PORT	16
#define VIP_VBI_STREAMS_PER_PORT	16

#define VIP_MAX_SUBDEV			5

#define VPDMA_FIRMWARE	"vpdma-1b8.bin"

/*
 * This value needs to be at least as large as the number of entry in
 * vip_formats[].
 * When vip_formats[] is modified make sure to adjust this value also.
 */
#define VIP_MAX_ACTIVE_FMT		16
/*
 * Colorspace conversion unit can be in one of 3 modes:
 * NA  - Not Available on this port
 * Y2R - Needed for YUV to RGB on this port
 * R2Y - Needed for RGB to YUV on this port
 */
enum vip_csc_state {
	VIP_CSC_NA = 0,
	VIP_CSC_Y2R,
	VIP_CSC_R2Y,
};

/* buffer for one video frame */
struct vip_buffer {
	/* common v4l buffer stuff */
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
	bool			drop;
};

/*
 * struct vip_fmt - VIP media bus format information
 * @fourcc: V4L2 pixel format FCC identifier
 * @code: V4L2 media bus format code
 * @colorspace: V4L2 colorspace identifier
 * @coplanar: 1 if unpacked Luma and Chroma, 0 otherwise (packed/interleaved)
 * @vpdma_fmt: VPDMA data format per plane.
 * @finfo: Cache v4l2_format_info for associated fourcc
 */
struct vip_fmt {
	u32	fourcc;
	u32	code;
	u32	colorspace;
	u8	coplanar;
	const struct vpdma_data_format *vpdma_fmt[VIP_MAX_PLANES];
	const struct v4l2_format_info *finfo;
};

/*
 * The vip_parser_data structures contains the memory mapped
 * info to access the parser registers.
 */
struct vip_parser_data {
	void __iomem		*base;

	struct platform_device *pdev;
};

/*
 * The vip_shared structure contains data that is shared by both
 * the VIP1 and VIP2 slices.
 */
struct vip_shared {
	struct list_head	list;
	void __iomem		*base;
	struct vpdma_data	vpdma_data;
	struct vpdma_data	*vpdma;
	struct v4l2_device	v4l2_dev;
	struct vip_dev		*devs[VIP_NUM_SLICES];
	struct v4l2_ctrl_handler ctrl_handler;
};

struct vip_ctrl_module {
	struct regmap	*syscon_pol;
	u32		syscon_offset;
	u32		syscon_bit_field[4];
};

/*
 * There are two vip_dev structure, one for each vip slice: VIP1 & VIP2.
 */
struct vip_dev {
	struct v4l2_device	v4l2_dev;
	struct platform_device	*pdev;
	struct vip_shared	*shared;
	struct vip_ctrl_module	*syscon;
	int			instance_id;
	int			slice_id;
	int			num_ports;	/* count of open ports */
	struct mutex		mutex;
	/* protects access to stream buffer queues */
	spinlock_t		slock;

	int			irq;
	void __iomem		*base;

	struct vip_port		*ports[VIP_NUM_PORTS];

	char			name[16];
	/* parser data handle */
	struct vip_parser_data	*parser;
	/* scaler data handle */
	struct sc_data		*sc;
	/* scaler port assignation */
	int			sc_assigned;
	/* csc data handle */
	struct csc_data		*csc;
	/* csc port assignation */
	int			csc_assigned;
};

/*
 * There are two vip_port structures for each vip_dev, one for port A
 * and one for port B.
 */
struct vip_port {
	struct vip_dev		*dev;
	int			port_id;

	unsigned int		flags;
	struct v4l2_rect	c_rect;		/* crop rectangle */
	struct v4l2_mbus_framefmt mbus_framefmt;
	struct v4l2_mbus_framefmt try_mbus_framefmt;

	char			name[16];
	struct vip_fmt		*fmt;		/* current format info */
	/* Number of channels/streams configured */
	int			num_streams_configured;
	int			num_streams;	/* count of open streams */
	struct vip_stream	*cap_streams[VIP_CAP_STREAMS_PER_PORT];

	struct v4l2_async_notifier notifier;
	struct v4l2_subdev	*subdev;
	struct v4l2_fwnode_endpoint endpoint;
	struct vip_fmt		*active_fmt[VIP_MAX_ACTIVE_FMT];
	int			num_active_fmt;
	/* have new shadow reg values */
	bool			load_mmrs;
	/* shadow reg addr/data block */
	struct vpdma_buf	mmr_adb;
	/* h coeff buffer */
	struct vpdma_buf	sc_coeff_h;
	/* v coeff buffer */
	struct vpdma_buf	sc_coeff_v;
	/* Show if scaler resource is available on this port */
	bool			scaler;
	/* Show the csc resource state on this port */
	enum vip_csc_state	csc;
};

/*
 * When handling multiplexed video, there can be multiple streams for each
 * port.  The vip_stream structure holds per-stream data.
 */
struct vip_stream {
	struct video_device	*vfd;
	struct vip_port		*port;
	int			stream_id;
	int			list_num;
	int			vfl_type;
	char			name[16];
	struct work_struct	recovery_work;
	int			num_recovery;
	enum v4l2_field		field;		/* current field */
	unsigned int		sequence;	/* current frame/field seq */
	enum v4l2_field		sup_field;	/* supported field value */
	unsigned int		width;		/* frame width */
	unsigned int		height;		/* frame height */
	unsigned int		bytesperline;	/* bytes per line in memory */
	unsigned int		sizeimage;	/* image size in memory */
	struct list_head	vidq;		/* incoming vip_bufs queue */
	struct list_head	dropq;		/* drop vip_bufs queue */
	struct list_head	post_bufs;	/* vip_bufs to be DMAed */
	/* Maintain a list of used channels - Needed for VPDMA cleanup */
	int			vpdma_channels[VPDMA_MAX_CHANNELS];
	int			vpdma_channels_to_abort[VPDMA_MAX_CHANNELS];
	struct vpdma_desc_list	desc_list;	/* DMA descriptor list */
	struct vpdma_dtd	*write_desc;
	/* next unused desc_list addr */
	void			*desc_next;
	struct vb2_queue	vb_vidq;
};

/*
 * VIP Enumerations
 */
enum data_path_select {
	ALL_FIELDS_DATA_SELECT = 0,
	VIP_CSC_SRC_DATA_SELECT,
	VIP_SC_SRC_DATA_SELECT,
	VIP_RGB_SRC_DATA_SELECT,
	VIP_RGB_OUT_LO_DATA_SELECT,
	VIP_RGB_OUT_HI_DATA_SELECT,
	VIP_CHR_DS_1_SRC_DATA_SELECT,
	VIP_CHR_DS_2_SRC_DATA_SELECT,
	VIP_MULTI_CHANNEL_DATA_SELECT,
	VIP_CHR_DS_1_DATA_BYPASS,
	VIP_CHR_DS_2_DATA_BYPASS,
};

enum data_interface_modes {
	SINGLE_24B_INTERFACE = 0,
	SINGLE_16B_INTERFACE = 1,
	DUAL_8B_INTERFACE = 2,
};

enum sync_types {
	EMBEDDED_SYNC_SINGLE_YUV422 = 0,
	EMBEDDED_SYNC_2X_MULTIPLEXED_YUV422 = 1,
	EMBEDDED_SYNC_4X_MULTIPLEXED_YUV422 = 2,
	EMBEDDED_SYNC_LINE_MULTIPLEXED_YUV422 = 3,
	DISCRETE_SYNC_SINGLE_YUV422 = 4,
	EMBEDDED_SYNC_SINGLE_RGB_OR_YUV444 = 5,
	DISCRETE_SYNC_SINGLE_RGB_24B = 10,
};

#define VIP_NOT_ASSIGNED	-1

/*
 * Register offsets and field selectors
 */
#define VIP_PID_FUNC			0xf02

#define VIP_PID				0x0000
#define VIP_PID_MINOR_MASK              0x3f
#define VIP_PID_MINOR_SHIFT             0
#define VIP_PID_CUSTOM_MASK             0x03
#define VIP_PID_CUSTOM_SHIFT            6
#define VIP_PID_MAJOR_MASK              0x07
#define VIP_PID_MAJOR_SHIFT             8
#define VIP_PID_RTL_MASK                0x1f
#define VIP_PID_RTL_SHIFT               11
#define VIP_PID_FUNC_MASK               0xfff
#define VIP_PID_FUNC_SHIFT              16
#define VIP_PID_SCHEME_MASK             0x03
#define VIP_PID_SCHEME_SHIFT            30

#define VIP_SYSCONFIG			0x0010
#define VIP_SYSCONFIG_IDLE_MASK         0x03
#define VIP_SYSCONFIG_IDLE_SHIFT        2
#define VIP_SYSCONFIG_STANDBY_MASK      0x03
#define VIP_SYSCONFIG_STANDBY_SHIFT     4
#define VIP_FORCE_IDLE_MODE             0
#define VIP_NO_IDLE_MODE                1
#define VIP_SMART_IDLE_MODE             2
#define VIP_SMART_IDLE_WAKEUP_MODE      3
#define VIP_FORCE_STANDBY_MODE          0
#define VIP_NO_STANDBY_MODE             1
#define VIP_SMART_STANDBY_MODE          2
#define VIP_SMART_STANDBY_WAKEUP_MODE   3

#define VIP_INTC_INTX_OFFSET		0x0020

#define VIP_INT0_STATUS0_RAW_SET	0x0020
#define VIP_INT0_STATUS0_RAW		VIP_INT0_STATUS0_RAW_SET
#define VIP_INT0_STATUS0_CLR		0x0028
#define VIP_INT0_STATUS0		VIP_INT0_STATUS0_CLR
#define VIP_INT0_ENABLE0_SET		0x0030
#define VIP_INT0_ENABLE0		VIP_INT0_ENABLE0_SET
#define VIP_INT0_ENABLE0_CLR		0x0038
#define VIP_INT0_LIST0_COMPLETE         BIT(0)
#define VIP_INT0_LIST0_NOTIFY           BIT(1)
#define VIP_INT0_LIST1_COMPLETE         BIT(2)
#define VIP_INT0_LIST1_NOTIFY           BIT(3)
#define VIP_INT0_LIST2_COMPLETE         BIT(4)
#define VIP_INT0_LIST2_NOTIFY           BIT(5)
#define VIP_INT0_LIST3_COMPLETE         BIT(6)
#define VIP_INT0_LIST3_NOTIFY           BIT(7)
#define VIP_INT0_LIST4_COMPLETE         BIT(8)
#define VIP_INT0_LIST4_NOTIFY           BIT(9)
#define VIP_INT0_LIST5_COMPLETE         BIT(10)
#define VIP_INT0_LIST5_NOTIFY           BIT(11)
#define VIP_INT0_LIST6_COMPLETE         BIT(12)
#define VIP_INT0_LIST6_NOTIFY           BIT(13)
#define VIP_INT0_LIST7_COMPLETE         BIT(14)
#define VIP_INT0_LIST7_NOTIFY           BIT(15)
#define VIP_INT0_DESCRIPTOR             BIT(16)
#define VIP_VIP1_PARSER_INT		BIT(20)
#define VIP_VIP2_PARSER_INT		BIT(21)

#define VIP_INT0_STATUS1_RAW_SET        0x0024
#define VIP_INT0_STATUS1_RAW            VIP_INT0_STATUS0_RAW_SET
#define VIP_INT0_STATUS1_CLR            0x002c
#define VIP_INT0_STATUS1                VIP_INT0_STATUS0_CLR
#define VIP_INT0_ENABLE1_SET            0x0034
#define VIP_INT0_ENABLE1                VIP_INT0_ENABLE0_SET
#define VIP_INT0_ENABLE1_CLR            0x003c
#define VIP_INT0_ENABLE1_STAT		0x004c
#define VIP_INT0_CHANNEL_GROUP0		BIT(0)
#define VIP_INT0_CHANNEL_GROUP1		BIT(1)
#define VIP_INT0_CHANNEL_GROUP2		BIT(2)
#define VIP_INT0_CHANNEL_GROUP3		BIT(3)
#define VIP_INT0_CHANNEL_GROUP4		BIT(4)
#define VIP_INT0_CHANNEL_GROUP5		BIT(5)
#define VIP_INT0_CLIENT			BIT(7)
#define VIP_VIP1_DS1_UV_ERROR_INT	BIT(22)
#define VIP_VIP1_DS2_UV_ERROR_INT	BIT(23)
#define VIP_VIP2_DS1_UV_ERROR_INT	BIT(24)
#define VIP_VIP2_DS2_UV_ERROR_INT	BIT(25)

#define VIP_INTC_E0I			0x00a0

#define VIP_CLK_ENABLE			0x0100
#define VIP_VPDMA_CLK_ENABLE		BIT(0)
#define VIP_VIP1_DATA_PATH_CLK_ENABLE	BIT(16)
#define VIP_VIP2_DATA_PATH_CLK_ENABLE	BIT(17)

#define VIP_CLK_RESET			0x0104
#define VIP_VPDMA_RESET			BIT(0)
#define VIP_VPDMA_CLK_RESET_MASK	0x1
#define VIP_VPDMA_CLK_RESET_SHIFT	0
#define VIP_DATA_PATH_CLK_RESET_MASK	0x1
#define VIP_VIP1_DATA_PATH_RESET_SHIFT	16
#define VIP_VIP2_DATA_PATH_RESET_SHIFT	17
#define VIP_VIP1_DATA_PATH_RESET	BIT(16)
#define VIP_VIP2_DATA_PATH_RESET	BIT(17)
#define VIP_VIP1_PARSER_RESET		BIT(18)
#define VIP_VIP2_PARSER_RESET		BIT(19)
#define VIP_VIP1_CSC_RESET		BIT(20)
#define VIP_VIP2_CSC_RESET		BIT(21)
#define VIP_VIP1_SC_RESET		BIT(22)
#define VIP_VIP2_SC_RESET		BIT(23)
#define VIP_VIP1_DS1_RESET		BIT(25)
#define VIP_VIP2_DS1_RESET		BIT(26)
#define VIP_VIP1_DS2_RESET		BIT(27)
#define VIP_VIP2_DS2_RESET		BIT(28)
#define VIP_MAIN_RESET			BIT(31)

#define VIP_VIP1_DATA_PATH_SELECT	0x010c
#define VIP_VIP2_DATA_PATH_SELECT	0x0110
#define VIP_CSC_SRC_SELECT_MASK		0x07
#define VIP_CSC_SRC_SELECT_SHFT		0
#define VIP_SC_SRC_SELECT_MASK		0x07
#define VIP_SC_SRC_SELECT_SHFT		3
#define VIP_RGB_SRC_SELECT		BIT(6)
#define VIP_RGB_OUT_LO_SRC_SELECT	BIT(7)
#define VIP_RGB_OUT_HI_SRC_SELECT	BIT(8)
#define VIP_DS1_SRC_SELECT_MASK		0x07
#define VIP_DS1_SRC_SELECT_SHFT		9
#define VIP_DS2_SRC_SELECT_MASK		0x07
#define VIP_DS2_SRC_SELECT_SHFT		12
#define VIP_MULTI_CHANNEL_SELECT	BIT(15)
#define VIP_DS1_BYPASS			BIT(16)
#define VIP_DS2_BYPASS			BIT(17)
#define VIP_TESTPORT_B_SELECT		BIT(26)
#define VIP_TESTPORT_A_SELECT		BIT(27)
#define VIP_DATAPATH_SELECT_MASK	0x0f
#define VIP_DATAPATH_SELECT_SHFT	28

#define VIP_PARSER_MAIN_CFG		0x0000
#define VIP_DATA_INTERFACE_MODE_MASK	0x03
#define VIP_DATA_INTERFACE_MODE_SHFT	0
#define VIP_CLIP_BLANK			BIT(4)
#define VIP_CLIP_ACTIVE			BIT(5)

#define VIP_SLICE0_PARSER		0x5500
#define VIP_SLICE1_PARSER		0x5a00
#define VIP_PARSER_PORTA_0		0x0004
#define VIP_PARSER_PORTB_0		0x000c
#define VIP_SYNC_TYPE_MASK		0x0f
#define VIP_SYNC_TYPE_SHFT		0
#define VIP_CTRL_CHANNEL_SEL_MASK	0x03
#define VIP_CTRL_CHANNEL_SEL_SHFT	4
#define VIP_ASYNC_FIFO_WR		BIT(6)
#define VIP_ASYNC_FIFO_RD		BIT(7)
#define VIP_PORT_ENABLE			BIT(8)
#define VIP_FID_POLARITY		BIT(9)
#define VIP_PIXCLK_EDGE_POLARITY	BIT(10)
#define VIP_HSYNC_POLARITY		BIT(11)
#define VIP_VSYNC_POLARITY		BIT(12)
#define VIP_ACTVID_POLARITY		BIT(13)
#define VIP_FID_DETECT_MODE		BIT(14)
#define VIP_USE_ACTVID_HSYNC_ONLY	BIT(15)
#define VIP_FID_SKEW_PRECOUNT_MASK	0x3f
#define VIP_FID_SKEW_PRECOUNT_SHFT	16
#define VIP_DISCRETE_BASIC_MODE		BIT(22)
#define VIP_SW_RESET			BIT(23)
#define VIP_FID_SKEW_POSTCOUNT_MASK	0x3f
#define VIP_FID_SKEW_POSTCOUNT_SHFT	24
#define VIP_ANALYZER_2X4X_SRCNUM_POS	BIT(30)
#define VIP_ANALYZER_FVH_ERR_COR_EN	BIT(31)

#define VIP_PARSER_PORTA_1		0x0008
#define VIP_PARSER_PORTB_1		0x0010
#define VIP_SRC0_NUMLINES_MASK		0x0fff
#define VIP_SRC0_NUMLINES_SHFT		0
#define VIP_ANC_CHAN_SEL_8B_MASK	0x03
#define VIP_ANC_CHAN_SEL_8B_SHFT	13
#define VIP_SRC0_NUMPIX_MASK		0x0fff
#define VIP_SRC0_NUMPIX_SHFT		16
#define VIP_REPACK_SEL_MASK		0x07
#define VIP_REPACK_SEL_SHFT		28

#define VIP_PARSER_FIQ_MASK		0x0014
#define VIP_PARSER_FIQ_CLR		0x0018
#define VIP_PARSER_FIQ_STATUS		0x001c
#define VIP_PORTA_VDET			BIT(0)
#define VIP_PORTB_VDET			BIT(1)
#define VIP_PORTA_ASYNC_FIFO_OF		BIT(2)
#define VIP_PORTB_ASYNC_FIFO_OF		BIT(3)
#define VIP_PORTA_OUTPUT_FIFO_YUV	BIT(4)
#define VIP_PORTA_OUTPUT_FIFO_ANC	BIT(6)
#define VIP_PORTB_OUTPUT_FIFO_YUV	BIT(7)
#define VIP_PORTB_OUTPUT_FIFO_ANC	BIT(9)
#define VIP_PORTA_CONN			BIT(10)
#define VIP_PORTA_DISCONN		BIT(11)
#define VIP_PORTB_CONN			BIT(12)
#define VIP_PORTB_DISCONN		BIT(13)
#define VIP_PORTA_SRC0_SIZE		BIT(14)
#define VIP_PORTB_SRC0_SIZE		BIT(15)
#define VIP_PORTA_YUV_PROTO_VIOLATION	BIT(16)
#define VIP_PORTA_ANC_PROTO_VIOLATION	BIT(17)
#define VIP_PORTB_YUV_PROTO_VIOLATION	BIT(18)
#define VIP_PORTB_ANC_PROTO_VIOLATION	BIT(19)
#define VIP_PORTA_CFG_DISABLE_COMPLETE	BIT(20)
#define VIP_PORTB_CFG_DISABLE_COMPLETE	BIT(21)

#define VIP_PARSER_PORTA_SOURCE_FID	0x0020
#define VIP_PARSER_PORTA_ENCODER_FID	0x0024
#define VIP_PARSER_PORTB_SOURCE_FID	0x0028
#define VIP_PARSER_PORTB_ENCODER_FID	0x002c

#define VIP_PARSER_PORTA_SRC0_SIZE	0x0030
#define VIP_PARSER_PORTB_SRC0_SIZE	0x0070
#define VIP_SOURCE_HEIGHT_MASK		0x0fff
#define VIP_SOURCE_HEIGHT_SHFT		0
#define VIP_SOURCE_WIDTH_MASK		0x0fff
#define VIP_SOURCE_WIDTH_SHFT		16

#define VIP_PARSER_PORTA_VDET_VEC	0x00b0
#define VIP_PARSER_PORTB_VDET_VEC	0x00b4

#define VIP_PARSER_PORTA_EXTRA2		0x00b8
#define VIP_PARSER_PORTB_EXTRA2		0x00c8
#define VIP_ANC_SKIP_NUMPIX_MASK	0x0fff
#define VIP_ANC_SKIP_NUMPIX_SHFT	0
#define VIP_ANC_BYPASS			BIT(15)
#define VIP_ANC_USE_NUMPIX_MASK		0x0fff
#define VIP_ANC_USE_NUMPIX_SHFT		16
#define VIP_ANC_TARGET_SRCNUM_MASK	0x0f
#define VIP_ANC_TARGET_SRCNUM_SHFT	28

#define VIP_PARSER_PORTA_EXTRA3		0x00bc
#define VIP_PARSER_PORTB_EXTRA3		0x00cc
#define VIP_ANC_SKIP_NUMLINES_MASK	0x0fff
#define VIP_ANC_SKIP_NUMLINES_SHFT	0
#define VIP_ANC_USE_NUMLINES_MASK	0x0fff
#define VIP_ANC_USE_NUMLINES_SHFT	16

#define VIP_PARSER_PORTA_EXTRA4		0x00c0
#define VIP_PARSER_PORTB_EXTRA4		0x00d0
#define VIP_ACT_SKIP_NUMPIX_MASK	0x0fff
#define VIP_ACT_SKIP_NUMPIX_SHFT	0
#define VIP_ACT_BYPASS			BIT(15)
#define VIP_ACT_USE_NUMPIX_MASK		0x0fff
#define VIP_ACT_USE_NUMPIX_SHFT		16
#define VIP_ACT_TARGET_SRCNUM_MASK	0x0f
#define VIP_ACT_TARGET_SRCNUM_SHFT	28

#define VIP_PARSER_PORTA_EXTRA5		0x00c4
#define VIP_PARSER_PORTB_EXTRA5		0x00d4
#define VIP_ACT_SKIP_NUMLINES_MASK	0x0fff
#define VIP_ACT_SKIP_NUMLINES_SHFT	0
#define VIP_ACT_USE_NUMLINES_MASK	0x0fff
#define VIP_ACT_USE_NUMLINES_SHFT	16

#define VIP_PARSER_PORTA_EXTRA6		0x00d8
#define VIP_PARSER_PORTB_EXTRA6		0x00dc
#define VIP_ANC_SRCNUM_STOP_IMM_SHFT	0
#define VIP_YUV_SRCNUM_STOP_IMM_SHFT	16

#define VIP_SLICE0_CSC			0x5700
#define VIP_SLICE1_CSC			0x5c00
#define VIP_CSC_CSC00			0x0200
#define VIP_CSC_A0_MASK			0x1fff
#define VIP_CSC_A0_SHFT			0
#define VIP_CSC_B0_MASK			0x1fff
#define VIP_CSC_B0_SHFT			16

#define VIP_CSC_CSC01			0x0204
#define VIP_CSC_C0_MASK			0x1fff
#define VIP_CSC_C0_SHFT			0
#define VIP_CSC_A1_MASK			0x1fff
#define VIP_CSC_A1_SHFT			16

#define VIP_CSC_CSC02			0x0208
#define VIP_CSC_B1_MASK			0x1fff
#define VIP_CSC_B1_SHFT			0
#define VIP_CSC_C1_MASK			0x1fff
#define VIP_CSC_C1_SHFT			16

#define VIP_CSC_CSC03			0x020c
#define VIP_CSC_A2_MASK			0x1fff
#define VIP_CSC_A2_SHFT			0
#define VIP_CSC_B2_MASK			0x1fff
#define VIP_CSC_B2_SHFT			16

#define VIP_CSC_CSC04			0x0210
#define VIP_CSC_C2_MASK			0x1fff
#define VIP_CSC_C2_SHFT			0
#define VIP_CSC_D0_MASK			0x0fff
#define VIP_CSC_D0_SHFT			16

#define VIP_CSC_CSC05			0x0214
#define VIP_CSC_D1_MASK			0x0fff
#define VIP_CSC_D1_SHFT			0
#define VIP_CSC_D2_MASK			0x0fff
#define VIP_CSC_D2_SHFT			16
#define VIP_CSC_BYPASS			BIT(28)

#define VIP_SLICE0_SC			0x5800
#define VIP_SLICE1_SC			0x5d00
#define VIP_SC_MP_SC0			0x0300
#define VIP_INTERLACE_O			BIT(0)
#define VIP_LINEAR			BIT(1)
#define VIP_SC_BYPASS			BIT(2)
#define VIP_INVT_FID			BIT(3)
#define VIP_USE_RAV			BIT(4)
#define VIP_ENABLE_EV			BIT(5)
#define VIP_AUTH_HS			BIT(6)
#define VIP_DCM_2X			BIT(7)
#define VIP_DCM_4X			BIT(8)
#define VIP_HP_BYPASS			BIT(9)
#define VIP_INTERLACE_I			BIT(10)
#define VIP_ENABLE_SIN2_VER_INTP	BIT(11)
#define VIP_Y_PK_EN			BIT(14)
#define VIP_TRIM			BIT(15)
#define VIP_SELFGEN_FID			BIT(16)

#define VIP_SC_MP_SC1			0x0304
#define VIP_ROW_ACC_INC_MASK		0x07ffffff
#define VIP_ROW_ACC_INC_SHFT		0

#define VIP_SC_MP_SC2			0x0308
#define VIP_ROW_ACC_OFFSET_MASK		0x0fffffff
#define VIP_ROW_ACC_OFFSET_SHFT		0

#define VIP_SC_MP_SC3			0x030c
#define VIP_ROW_ACC_OFFSET_B_MASK	0x0fffffff
#define VIP_ROW_ACC_OFFSET_B_SHFT	0

#define VIP_SC_MP_SC4			0x0310
#define VIP_TAR_H_MASK			0x07ff
#define VIP_TAR_H_SHFT			0
#define VIP_TAR_W_MASK			0x07ff
#define VIP_TAR_W_SHFT			12
#define VIP_LIN_ACC_INC_U_MASK		0x07
#define VIP_LIN_ACC_INC_U_SHFT		24
#define VIP_NLIN_ACC_INIT_U_MASK	0x07
#define VIP_NLIN_ACC_INIT_U_SHFT	28

#define VIP_SC_MP_SC5			0x0314
#define VIP_SRC_H_MASK			0x03ff
#define VIP_SRC_H_SHFT			0
#define VIP_SRC_W_MASK			0x07ff
#define VIP_SRC_W_SHFT			12
#define VIP_NLIN_ACC_INC_U_MASK		0x07
#define VIP_NLIN_ACC_INC_U_SHFT		24

#define VIP_SC_MP_SC6			0x0318
#define VIP_ROW_ACC_INIT_RAV_MASK	0x03ff
#define VIP_ROW_ACC_INIT_RAV_SHFT	0
#define VIP_ROW_ACC_INIT_RAV_B_MASK	0x03ff
#define VIP_ROW_ACC_INIT_RAV_B_SHFT	10

#define VIP_SC_MP_SC8			0x0320
#define VIP_NLIN_LEFT_MASK		0x07ff
#define VIP_NLIN_LEFT_SHFT		0
#define VIP_NLIN_RIGHT_MASK		0x07ff
#define VIP_NLIN_RIGHT_SHFT		12

#define VIP_SC_MP_SC9			0x0324
#define VIP_LIN_ACC_INC			VIP_SC_MP_SC9

#define VIP_SC_MP_SC10			0x0328
#define VIP_NLIN_ACC_INIT		VIP_SC_MP_SC10

#define VIP_SC_MP_SC11			0x032c
#define VIP_NLIN_ACC_INC		VIP_SC_MP_SC11

#define VIP_SC_MP_SC12			0x0330
#define VIP_COL_ACC_OFFSET_MASK		0x01ffffff
#define VIP_COL_ACC_OFFSET_SHFT		0

#define VIP_SC_MP_SC13			0x0334
#define VIP_SC_FACTOR_RAV_MASK		0x03ff
#define VIP_SC_FACTOR_RAV_SHFT		0
#define VIP_CHROMA_INTP_THR_MASK	0x03ff
#define VIP_CHROMA_INTP_THR_SHFT	12
#define VIP_DELTA_CHROMA_THR_MASK	0x0f
#define VIP_DELTA_CHROMA_THR_SHFT	24

#define VIP_SC_MP_SC17			0x0344
#define VIP_EV_THR_MASK			0x03ff
#define VIP_EV_THR_SHFT			12
#define VIP_DELTA_LUMA_THR_MASK		0x0f
#define VIP_DELTA_LUMA_THR_SHFT		24
#define VIP_DELTA_EV_THR_MASK		0x0f
#define VIP_DELTA_EV_THR_SHFT		28

#define VIP_SC_MP_SC18			0x0348
#define VIP_HS_FACTOR_MASK		0x03ff
#define VIP_HS_FACTOR_SHFT		0
#define VIP_CONF_DEFAULT_MASK		0x01ff
#define VIP_CONF_DEFAULT_SHFT		16

#define VIP_SC_MP_SC19			0x034c
#define VIP_HPF_COEFF0_MASK		0xff
#define VIP_HPF_COEFF0_SHFT		0
#define VIP_HPF_COEFF1_MASK		0xff
#define VIP_HPF_COEFF1_SHFT		8
#define VIP_HPF_COEFF2_MASK		0xff
#define VIP_HPF_COEFF2_SHFT		16
#define VIP_HPF_COEFF3_MASK		0xff
#define VIP_HPF_COEFF3_SHFT		23

#define VIP_SC_MP_SC20			0x0350
#define VIP_HPF_COEFF4_MASK		0xff
#define VIP_HPF_COEFF4_SHFT		0
#define VIP_HPF_COEFF5_MASK		0xff
#define VIP_HPF_COEFF5_SHFT		8
#define VIP_HPF_NORM_SHFT_MASK		0x07
#define VIP_HPF_NORM_SHFT_SHFT		16
#define VIP_NL_LIMIT_MASK		0x1ff
#define VIP_NL_LIMIT_SHFT		20

#define VIP_SC_MP_SC21			0x0354
#define VIP_NL_LO_THR_MASK		0x01ff
#define VIP_NL_LO_THR_SHFT		0
#define VIP_NL_LO_SLOPE_MASK		0xff
#define VIP_NL_LO_SLOPE_SHFT		16

#define VIP_SC_MP_SC22			0x0358
#define VIP_NL_HI_THR_MASK		0x01ff
#define VIP_NL_HI_THR_SHFT		0
#define VIP_NL_HI_SLOPE_SH_MASK		0x07
#define VIP_NL_HI_SLOPE_SH_SHFT		16

#define VIP_SC_MP_SC23			0x035c
#define VIP_GRADIENT_THR_MASK		0x07ff
#define VIP_GRADIENT_THR_SHFT		0
#define VIP_GRADIENT_THR_RANGE_MASK	0x0f
#define VIP_GRADIENT_THR_RANGE_SHFT	12
#define VIP_MIN_GY_THR_MASK		0xff
#define VIP_MIN_GY_THR_SHFT		16
#define VIP_MIN_GY_THR_RANGE_MASK	0x0f
#define VIP_MIN_GY_THR_RANGE_SHFT	28

#define VIP_SC_MP_SC24			0x0360
#define VIP_ORG_H_MASK			0x07ff
#define VIP_ORG_H_SHFT			0
#define VIP_ORG_W_MASK			0x07ff
#define VIP_ORG_W_SHFT			16

#define VIP_SC_MP_SC25			0x0364
#define VIP_OFF_H_MASK			0x07ff
#define VIP_OFF_H_SHFT			0
#define VIP_OFF_W_MASK			0x07ff
#define VIP_OFF_W_SHFT			16

#define VIP_VPDMA_BASE			0xd000

#endif
