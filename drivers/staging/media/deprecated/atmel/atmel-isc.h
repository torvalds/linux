/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Microchip Image Sensor Controller (ISC) driver header file
 *
 * Copyright (C) 2016-2019 Microchip Technology, Inc.
 *
 * Author: Songjun Wu
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 *
 */
#ifndef _ATMEL_ISC_H_

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-contig.h>

#define ISC_CLK_MAX_DIV		255

enum isc_clk_id {
	ISC_ISPCK = 0,
	ISC_MCK = 1,
};

struct isc_clk {
	struct clk_hw   hw;
	struct clk      *clk;
	struct regmap   *regmap;
	spinlock_t	lock;	/* serialize access to clock registers */
	u8		id;
	u8		parent_id;
	u32		div;
	struct device	*dev;
};

#define to_isc_clk(v) container_of(v, struct isc_clk, hw)

struct isc_buffer {
	struct vb2_v4l2_buffer  vb;
	struct list_head	list;
};

struct isc_subdev_entity {
	struct v4l2_subdev		*sd;
	struct v4l2_async_subdev	*asd;
	struct device_node		*epn;
	struct v4l2_async_notifier      notifier;

	u32 pfe_cfg0;

	struct list_head list;
};

/*
 * struct isc_format - ISC media bus format information
			This structure represents the interface between the ISC
			and the sensor. It's the input format received by
			the ISC.
 * @fourcc:		Fourcc code for this format
 * @mbus_code:		V4L2 media bus format code.
 * @cfa_baycfg:		If this format is RAW BAYER, indicate the type of bayer.
			this is either BGBG, RGRG, etc.
 * @pfe_cfg0_bps:	Number of hardware data lines connected to the ISC
 */

struct isc_format {
	u32	fourcc;
	u32	mbus_code;
	u32	cfa_baycfg;

	bool	sd_support;
	u32	pfe_cfg0_bps;
};

/* Pipeline bitmap */
#define DPC_DPCENABLE	BIT(0)
#define DPC_GDCENABLE	BIT(1)
#define DPC_BLCENABLE	BIT(2)
#define WB_ENABLE	BIT(3)
#define CFA_ENABLE	BIT(4)
#define CC_ENABLE	BIT(5)
#define GAM_ENABLE	BIT(6)
#define GAM_BENABLE	BIT(7)
#define GAM_GENABLE	BIT(8)
#define GAM_RENABLE	BIT(9)
#define VHXS_ENABLE	BIT(10)
#define CSC_ENABLE	BIT(11)
#define CBC_ENABLE	BIT(12)
#define SUB422_ENABLE	BIT(13)
#define SUB420_ENABLE	BIT(14)

#define GAM_ENABLES	(GAM_RENABLE | GAM_GENABLE | GAM_BENABLE | GAM_ENABLE)

/*
 * struct fmt_config - ISC format configuration and internal pipeline
			This structure represents the internal configuration
			of the ISC.
			It also holds the format that ISC will present to v4l2.
 * @sd_format:		Pointer to an isc_format struct that holds the sensor
			configuration.
 * @fourcc:		Fourcc code for this format.
 * @bpp:		Bytes per pixel in the current format.
 * @bpp_v4l2:		Bytes per pixel in the current format, for v4l2.
			This differs from 'bpp' in the sense that in planar
			formats, it refers only to the first plane.
 * @rlp_cfg_mode:	Configuration of the RLP (rounding, limiting packaging)
 * @dcfg_imode:		Configuration of the input of the DMA module
 * @dctrl_dview:	Configuration of the output of the DMA module
 * @bits_pipeline:	Configuration of the pipeline, which modules are enabled
 */
struct fmt_config {
	struct isc_format	*sd_format;

	u32			fourcc;
	u8			bpp;
	u8			bpp_v4l2;

	u32			rlp_cfg_mode;
	u32			dcfg_imode;
	u32			dctrl_dview;

	u32			bits_pipeline;
};

#define HIST_ENTRIES		512
#define HIST_BAYER		(ISC_HIS_CFG_MODE_B + 1)

enum{
	HIST_INIT = 0,
	HIST_ENABLED,
	HIST_DISABLED,
};

struct isc_ctrls {
	struct v4l2_ctrl_handler handler;

	u32 brightness;
	u32 contrast;
	u8 gamma_index;
#define ISC_WB_NONE	0
#define ISC_WB_AUTO	1
#define ISC_WB_ONETIME	2
	u8 awb;

	/* one for each component : GR, R, GB, B */
	u32 gain[HIST_BAYER];
	s32 offset[HIST_BAYER];

	u32 hist_entry[HIST_ENTRIES];
	u32 hist_count[HIST_BAYER];
	u8 hist_id;
	u8 hist_stat;
#define HIST_MIN_INDEX		0
#define HIST_MAX_INDEX		1
	u32 hist_minmax[HIST_BAYER][2];
};

#define ISC_PIPE_LINE_NODE_NUM	15

/*
 * struct isc_reg_offsets - ISC device register offsets
 * @csc:		Offset for the CSC register
 * @cbc:		Offset for the CBC register
 * @sub422:		Offset for the SUB422 register
 * @sub420:		Offset for the SUB420 register
 * @rlp:		Offset for the RLP register
 * @his:		Offset for the HIS related registers
 * @dma:		Offset for the DMA related registers
 * @version:		Offset for the version register
 * @his_entry:		Offset for the HIS entries registers
 */
struct isc_reg_offsets {
	u32 csc;
	u32 cbc;
	u32 sub422;
	u32 sub420;
	u32 rlp;
	u32 his;
	u32 dma;
	u32 version;
	u32 his_entry;
};

/*
 * struct isc_device - ISC device driver data/config struct
 * @regmap:		Register map
 * @hclock:		Hclock clock input (refer datasheet)
 * @ispck:		iscpck clock (refer datasheet)
 * @isc_clks:		ISC clocks
 * @ispck_required:	ISC requires ISP Clock initialization
 * @dcfg:		DMA master configuration, architecture dependent
 *
 * @dev:		Registered device driver
 * @v4l2_dev:		v4l2 registered device
 * @video_dev:		registered video device
 *
 * @vb2_vidq:		video buffer 2 video queue
 * @dma_queue_lock:	lock to serialize the dma buffer queue
 * @dma_queue:		the queue for dma buffers
 * @cur_frm:		current isc frame/buffer
 * @sequence:		current frame number
 * @stop:		true if isc is not streaming, false if streaming
 * @comp:		completion reference that signals frame completion
 *
 * @fmt:		current v42l format
 * @user_formats:	list of formats that are supported and agreed with sd
 * @num_user_formats:	how many formats are in user_formats
 *
 * @config:		current ISC format configuration
 * @try_config:		the current ISC try format , not yet activated
 *
 * @ctrls:		holds information about ISC controls
 * @do_wb_ctrl:		control regarding the DO_WHITE_BALANCE button
 * @awb_work:		workqueue reference for autowhitebalance histogram
 *			analysis
 *
 * @lock:		lock for serializing userspace file operations
 *			with ISC operations
 * @awb_mutex:		serialize access to streaming status from awb work queue
 * @awb_lock:		lock for serializing awb work queue operations
 *			with DMA/buffer operations
 *
 * @pipeline:		configuration of the ISC pipeline
 *
 * @current_subdev:	current subdevice: the sensor
 * @subdev_entities:	list of subdevice entitites
 *
 * @gamma_table:	pointer to the table with gamma values, has
 *			gamma_max sets of GAMMA_ENTRIES entries each
 * @gamma_max:		maximum number of sets of inside the gamma_table
 *
 * @max_width:		maximum frame width, dependent on the internal RAM
 * @max_height:		maximum frame height, dependent on the internal RAM
 *
 * @config_dpc:		pointer to a function that initializes product
 *			specific DPC module
 * @config_csc:		pointer to a function that initializes product
 *			specific CSC module
 * @config_cbc:		pointer to a function that initializes product
 *			specific CBC module
 * @config_cc:		pointer to a function that initializes product
 *			specific CC module
 * @config_gam:		pointer to a function that initializes product
 *			specific GAMMA module
 * @config_rlp:		pointer to a function that initializes product
 *			specific RLP module
 * @config_ctrls:	pointer to a functoin that initializes product
 *			specific v4l2 controls.
 *
 * @adapt_pipeline:	pointer to a function that adapts the pipeline bits
 *			to the product specific pipeline
 *
 * @offsets:		struct holding the product specific register offsets
 * @controller_formats:	pointer to the array of possible formats that the
 *			controller can output
 * @formats_list:	pointer to the array of possible formats that can
 *			be used as an input to the controller
 * @controller_formats_size:	size of controller_formats array
 * @formats_list_size:	size of formats_list array
 */
struct isc_device {
	struct regmap		*regmap;
	struct clk		*hclock;
	struct clk		*ispck;
	struct isc_clk		isc_clks[2];
	bool			ispck_required;
	u32			dcfg;

	struct device		*dev;
	struct v4l2_device	v4l2_dev;
	struct video_device	video_dev;

	struct vb2_queue	vb2_vidq;
	spinlock_t		dma_queue_lock;
	struct list_head	dma_queue;
	struct isc_buffer	*cur_frm;
	unsigned int		sequence;
	bool			stop;
	struct completion	comp;

	struct v4l2_format	fmt;
	struct isc_format	**user_formats;
	unsigned int		num_user_formats;

	struct fmt_config	config;
	struct fmt_config	try_config;

	struct isc_ctrls	ctrls;
	struct work_struct	awb_work;

	struct mutex		lock;
	struct mutex		awb_mutex;
	spinlock_t		awb_lock;

	struct regmap_field	*pipeline[ISC_PIPE_LINE_NODE_NUM];

	struct isc_subdev_entity	*current_subdev;
	struct list_head		subdev_entities;

	struct {
#define ISC_CTRL_DO_WB 1
#define ISC_CTRL_R_GAIN 2
#define ISC_CTRL_B_GAIN 3
#define ISC_CTRL_GR_GAIN 4
#define ISC_CTRL_GB_GAIN 5
#define ISC_CTRL_R_OFF 6
#define ISC_CTRL_B_OFF 7
#define ISC_CTRL_GR_OFF 8
#define ISC_CTRL_GB_OFF 9
		struct v4l2_ctrl	*awb_ctrl;
		struct v4l2_ctrl	*do_wb_ctrl;
		struct v4l2_ctrl	*r_gain_ctrl;
		struct v4l2_ctrl	*b_gain_ctrl;
		struct v4l2_ctrl	*gr_gain_ctrl;
		struct v4l2_ctrl	*gb_gain_ctrl;
		struct v4l2_ctrl	*r_off_ctrl;
		struct v4l2_ctrl	*b_off_ctrl;
		struct v4l2_ctrl	*gr_off_ctrl;
		struct v4l2_ctrl	*gb_off_ctrl;
	};

#define GAMMA_ENTRIES	64
	/* pointer to the defined gamma table */
	const u32	(*gamma_table)[GAMMA_ENTRIES];
	u32		gamma_max;

	u32		max_width;
	u32		max_height;

	struct {
		void (*config_dpc)(struct isc_device *isc);
		void (*config_csc)(struct isc_device *isc);
		void (*config_cbc)(struct isc_device *isc);
		void (*config_cc)(struct isc_device *isc);
		void (*config_gam)(struct isc_device *isc);
		void (*config_rlp)(struct isc_device *isc);

		void (*config_ctrls)(struct isc_device *isc,
				     const struct v4l2_ctrl_ops *ops);

		void (*adapt_pipeline)(struct isc_device *isc);
	};

	struct isc_reg_offsets		offsets;
	const struct isc_format		*controller_formats;
	struct isc_format		*formats_list;
	u32				controller_formats_size;
	u32				formats_list_size;
};

extern const struct regmap_config atmel_isc_regmap_config;
extern const struct v4l2_async_notifier_operations atmel_isc_async_ops;

irqreturn_t atmel_isc_interrupt(int irq, void *dev_id);
int atmel_isc_pipeline_init(struct isc_device *isc);
int atmel_isc_clk_init(struct isc_device *isc);
void atmel_isc_subdev_cleanup(struct isc_device *isc);
void atmel_isc_clk_cleanup(struct isc_device *isc);

#endif
