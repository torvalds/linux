/*
 * Copyright 2005-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __INCLUDE_IPU_PRV_H__
#define __INCLUDE_IPU_PRV_H__

#include <linux/clkdev.h>
#include <linux/device.h>
#include <linux/fsl_devices.h>
#include <linux/interrupt.h>
#include <linux/types.h>

#define MXC_IPU_MAX_NUM		2
#define MXC_DI_NUM_PER_IPU	2

/* Globals */
extern int dmfc_type_setup;

#define IDMA_CHAN_INVALID	0xFF
#define HIGH_RESOLUTION_WIDTH	1024

enum ipuv3_type {
	IPUv3D,		/* i.MX37 */
	IPUv3EX,	/* i.MX51 */
	IPUv3M,		/* i.MX53 */
	IPUv3H,		/* i.MX6Q/SDL */
};

#define IPU_MAX_VDI_IN_WIDTH(type)	({ (type) >= IPUv3M ? 968 : 720; })

struct ipu_irq_node {
	irqreturn_t(*handler) (int, void *);	/*!< the ISR */
	const char *name;	/*!< device associated with the interrupt */
	void *dev_id;		/*!< some unique information for the ISR */
	__u32 flags;		/*!< not used */
};

enum csc_type_t {
	RGB2YUV = 0,
	YUV2RGB,
	RGB2RGB,
	YUV2YUV,
	CSC_NONE,
	CSC_NUM
};

struct ipu_soc {
	unsigned int id;
	unsigned int devtype;
	bool online;

	/*clk*/
	struct clk *ipu_clk;
	struct clk *di_clk[2];
	struct clk *di_clk_sel[2];
	struct clk *pixel_clk[2];
	bool pixel_clk_en[2];
	struct clk *pixel_clk_sel[2];
	struct clk *csi_clk[2];
	struct clk *prg_clk;

	/*irq*/
	int irq_sync;
	int irq_err;
	struct ipu_irq_node irq_list[IPU_IRQ_COUNT];

	/*reg*/
	void __iomem *cm_reg;
	void __iomem *idmac_reg;
	void __iomem *dp_reg;
	void __iomem *ic_reg;
	void __iomem *dc_reg;
	void __iomem *dc_tmpl_reg;
	void __iomem *dmfc_reg;
	void __iomem *di_reg[2];
	void __iomem *smfc_reg;
	void __iomem *csi_reg[2];
	void __iomem *cpmem_base;
	void __iomem *tpmem_base;
	void __iomem *vdi_reg;

	struct device *dev;

	ipu_channel_t csi_channel[2];
	ipu_channel_t using_ic_dirct_ch;
	unsigned char dc_di_assignment[10];
	bool sec_chan_en[24];
	bool thrd_chan_en[24];
	bool chan_is_interlaced[52];
	uint32_t channel_init_mask;
	uint32_t channel_enable_mask;

	/*use count*/
	int dc_use_count;
	int dp_use_count;
	int dmfc_use_count;
	int smfc_use_count;
	int ic_use_count;
	int rot_use_count;
	int vdi_use_count;
	int di_use_count[2];
	int csi_use_count[2];

	struct mutex mutex_lock;
	spinlock_t int_reg_spin_lock;
	spinlock_t rdy_reg_spin_lock;

	int dmfc_size_28;
	int dmfc_size_29;
	int dmfc_size_24;
	int dmfc_size_27;
	int dmfc_size_23;

	enum csc_type_t fg_csc_type;
	enum csc_type_t bg_csc_type;
	bool color_key_4rgb;
	bool dc_swap;
	struct completion dc_comp;
	struct completion csi_comp;

	struct rot_mem {
		void *vaddr;
		dma_addr_t paddr;
		int size;
	} rot_dma[2];

	int	vdoa_en;
	struct task_struct *thread[2];

	/*
	 * Bypass reset to avoid display channel being
	 * stopped by probe since it may starts to work
	 * in bootloader.
	 */
	bool bypass_reset;

	unsigned int ch0123_axi;
	unsigned int ch23_axi;
	unsigned int ch27_axi;
	unsigned int ch28_axi;
	unsigned int normal_axi;

	bool smfc_idmac_12bit_3planar_bs_fixup;	/* workaround little stripes */
};

struct ipu_channel {
	u8 video_in_dma;
	u8 alpha_in_dma;
	u8 graph_in_dma;
	u8 out_dma;
};

enum ipu_dmfc_type {
	DMFC_NORMAL = 0,
	DMFC_HIGH_RESOLUTION_DC,
	DMFC_HIGH_RESOLUTION_DP,
	DMFC_HIGH_RESOLUTION_ONLY_DP,
};

static inline int _ipu_is_smfc_chan(uint32_t dma_chan)
{
	return dma_chan <= 3;
}

static inline u32 ipu_cm_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->cm_reg + offset);
}

static inline void ipu_cm_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->cm_reg + offset);
}

static inline u32 ipu_idmac_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->idmac_reg + offset);
}

static inline void ipu_idmac_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->idmac_reg + offset);
}

static inline u32 ipu_dc_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->dc_reg + offset);
}

static inline void ipu_dc_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->dc_reg + offset);
}

static inline u32 ipu_dc_tmpl_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->dc_tmpl_reg + offset);
}

static inline void ipu_dc_tmpl_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->dc_tmpl_reg + offset);
}

static inline u32 ipu_dmfc_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->dmfc_reg + offset);
}

static inline void ipu_dmfc_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->dmfc_reg + offset);
}

static inline u32 ipu_dp_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->dp_reg + offset);
}

static inline void ipu_dp_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->dp_reg + offset);
}

static inline u32 ipu_di_read(struct ipu_soc *ipu, int di, unsigned offset)
{
	return readl(ipu->di_reg[di] + offset);
}

static inline void ipu_di_write(struct ipu_soc *ipu, int di,
		u32 value, unsigned offset)
{
	writel(value, ipu->di_reg[di] + offset);
}

static inline u32 ipu_csi_read(struct ipu_soc *ipu, int csi, unsigned offset)
{
	return readl(ipu->csi_reg[csi] + offset);
}

static inline void ipu_csi_write(struct ipu_soc *ipu, int csi,
		u32 value, unsigned offset)
{
	writel(value, ipu->csi_reg[csi] + offset);
}

static inline u32 ipu_smfc_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->smfc_reg + offset);
}

static inline void ipu_smfc_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->smfc_reg + offset);
}

static inline u32 ipu_vdi_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->vdi_reg + offset);
}

static inline void ipu_vdi_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->vdi_reg + offset);
}

static inline u32 ipu_ic_read(struct ipu_soc *ipu, unsigned offset)
{
	return readl(ipu->ic_reg + offset);
}

static inline void ipu_ic_write(struct ipu_soc *ipu,
		u32 value, unsigned offset)
{
	writel(value, ipu->ic_reg + offset);
}

int register_ipu_device(struct ipu_soc *ipu, int id);
void unregister_ipu_device(struct ipu_soc *ipu, int id);
ipu_color_space_t format_to_colorspace(uint32_t fmt);
bool ipu_pixel_format_has_alpha(uint32_t fmt);

void ipu_dump_registers(struct ipu_soc *ipu);

uint32_t _ipu_channel_status(struct ipu_soc *ipu, ipu_channel_t channel);

void ipu_disp_init(struct ipu_soc *ipu);
void _ipu_init_dc_mappings(struct ipu_soc *ipu);
int _ipu_dp_init(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t in_pixel_fmt,
		 uint32_t out_pixel_fmt);
void _ipu_dp_uninit(struct ipu_soc *ipu, ipu_channel_t channel);
void _ipu_dc_init(struct ipu_soc *ipu, int dc_chan, int di, bool interlaced, uint32_t pixel_fmt);
void _ipu_dc_uninit(struct ipu_soc *ipu, int dc_chan);
void _ipu_dp_dc_enable(struct ipu_soc *ipu, ipu_channel_t channel);
void _ipu_dp_dc_disable(struct ipu_soc *ipu, ipu_channel_t channel, bool swap);
void _ipu_dmfc_init(struct ipu_soc *ipu, int dmfc_type, int first);
void _ipu_dmfc_set_wait4eot(struct ipu_soc *ipu, int dma_chan, int width);
void _ipu_dmfc_set_burst_size(struct ipu_soc *ipu, int dma_chan, int burst_size);
int _ipu_disp_chan_is_interlaced(struct ipu_soc *ipu, ipu_channel_t channel);

void _ipu_ic_enable_task(struct ipu_soc *ipu, ipu_channel_t channel);
void _ipu_ic_disable_task(struct ipu_soc *ipu, ipu_channel_t channel);
int  _ipu_ic_init_prpvf(struct ipu_soc *ipu, ipu_channel_params_t *params,
			bool src_is_csi);
void _ipu_vdi_init(struct ipu_soc *ipu, ipu_channel_t channel, ipu_channel_params_t *params);
void _ipu_vdi_uninit(struct ipu_soc *ipu);
void _ipu_ic_uninit_prpvf(struct ipu_soc *ipu);
void _ipu_ic_init_rotate_vf(struct ipu_soc *ipu, ipu_channel_params_t *params);
void _ipu_ic_uninit_rotate_vf(struct ipu_soc *ipu);
void _ipu_ic_init_csi(struct ipu_soc *ipu, ipu_channel_params_t *params);
void _ipu_ic_uninit_csi(struct ipu_soc *ipu);
int  _ipu_ic_init_prpenc(struct ipu_soc *ipu, ipu_channel_params_t *params,
			 bool src_is_csi);
void _ipu_ic_uninit_prpenc(struct ipu_soc *ipu);
void _ipu_ic_init_rotate_enc(struct ipu_soc *ipu, ipu_channel_params_t *params);
void _ipu_ic_uninit_rotate_enc(struct ipu_soc *ipu);
int  _ipu_ic_init_pp(struct ipu_soc *ipu, ipu_channel_params_t *params);
void _ipu_ic_uninit_pp(struct ipu_soc *ipu);
void _ipu_ic_init_rotate_pp(struct ipu_soc *ipu, ipu_channel_params_t *params);
void _ipu_ic_uninit_rotate_pp(struct ipu_soc *ipu);
int _ipu_ic_idma_init(struct ipu_soc *ipu, int dma_chan, uint16_t width, uint16_t height,
		      int burst_size, ipu_rotate_mode_t rot);
void _ipu_vdi_toggle_top_field_man(struct ipu_soc *ipu);
int _ipu_csi_init(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t csi);
int _ipu_csi_set_mipi_di(struct ipu_soc *ipu, uint32_t num, uint32_t di_val, uint32_t csi);
void ipu_csi_set_test_generator(struct ipu_soc *ipu, bool active, uint32_t r_value,
		uint32_t g_value, uint32_t b_value,
		uint32_t pix_clk, uint32_t csi);
void _ipu_csi_ccir_err_detection_enable(struct ipu_soc *ipu, uint32_t csi);
void _ipu_csi_ccir_err_detection_disable(struct ipu_soc *ipu, uint32_t csi);
void _ipu_csi_wait4eof(struct ipu_soc *ipu, ipu_channel_t channel);
void _ipu_smfc_init(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t mipi_id, uint32_t csi);
void _ipu_smfc_set_burst_size(struct ipu_soc *ipu, ipu_channel_t channel, uint32_t bs);
void _ipu_dp_set_csc_coefficients(struct ipu_soc *ipu, ipu_channel_t channel, int32_t param[][3]);
int32_t _ipu_disp_set_window_pos(struct ipu_soc *ipu, ipu_channel_t channel,
		int16_t x_pos, int16_t y_pos);
int32_t _ipu_disp_get_window_pos(struct ipu_soc *ipu, ipu_channel_t channel,
		int16_t *x_pos, int16_t *y_pos);
void _ipu_get(struct ipu_soc *ipu);
void _ipu_put(struct ipu_soc *ipu);

struct clk *clk_register_mux_pix_clk(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, unsigned long flags,
		u8 ipu_id, u8 di_id, u8 clk_mux_flags);
struct clk *clk_register_div_pix_clk(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		u8 ipu_id, u8 di_id, u8 clk_div_flags);
struct clk *clk_register_gate_pix_clk(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		u8 ipu_id, u8 di_id, u8 clk_gate_flags);
#endif				/* __INCLUDE_IPU_PRV_H__ */
