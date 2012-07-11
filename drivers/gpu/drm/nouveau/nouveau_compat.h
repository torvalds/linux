#ifndef __NOUVEAU_COMPAT_H__
#define __NOUVEAU_COMPAT_H__

int nvdrm_gart_init(struct drm_device *, u64 *, u64 *);

u8   _nv_rd08(struct drm_device *, u32);
void _nv_wr08(struct drm_device *, u32, u8);
u32  _nv_rd32(struct drm_device *, u32);
void _nv_wr32(struct drm_device *, u32, u32);
u32  _nv_mask(struct drm_device *, u32, u32, u32);

bool _nv_bios(struct drm_device *, u8 **, u32 *);

struct dcb_gpio_func;
void nouveau_gpio_reset(struct drm_device *);
int  nouveau_gpio_find(struct drm_device *, int, u8, u8, struct dcb_gpio_func *);
bool nouveau_gpio_func_valid(struct drm_device *, u8 tag);
int  nouveau_gpio_func_set(struct drm_device *, u8 tag, int state);
int  nouveau_gpio_func_get(struct drm_device *, u8 tag);
int  nouveau_gpio_irq(struct drm_device *, int idx, u8 tag, u8 line, bool on);
int  nouveau_gpio_isr_add(struct drm_device *, int idx, u8 tag, u8 line,
			  void (*)(void *, int state), void *data);
void nouveau_gpio_isr_del(struct drm_device *, int idx, u8 tag, u8 line,
			  void (*)(void *, int state), void *data);

struct nouveau_i2c_port *nouveau_i2c_find(struct drm_device *, u8);
bool nouveau_probe_i2c_addr(struct nouveau_i2c_port *, int addr);
struct i2c_adapter *nouveau_i2c_adapter(struct nouveau_i2c_port *);
int nouveau_i2c_identify(struct drm_device *dev, const char *what,
			 struct i2c_board_info *info,
			 bool (*match)(struct nouveau_i2c_port *,
				       struct i2c_board_info *), int index);

int auxch_rd(struct drm_device *, struct nouveau_i2c_port *, u32, u8 *, u8);
int auxch_wr(struct drm_device *, struct nouveau_i2c_port *, u32, u8 *, u8);

struct nvbios_pll;
struct nouveau_pll_vals;

u32 get_pll_register(struct drm_device *dev, u32 type);
int get_pll_limits(struct drm_device *, u32, struct nvbios_pll *);
int setPLL(struct drm_device *, u32 reg, u32 clk);

int nouveau_calc_pll_mnp(struct drm_device *, struct nvbios_pll *,
			 int, struct nouveau_pll_vals *);
int nva3_calc_pll(struct drm_device *dev, struct nvbios_pll *info, u32 freq,
	      int *N, int *fN, int *M, int *P);
int nouveau_hw_setpll(struct drm_device *, u32, struct nouveau_pll_vals *);

struct dcb_output;
void nouveau_bios_run_init_table(struct drm_device *, u16, struct dcb_output *, int);
void nouveau_bios_init_exec(struct drm_device *, u16);

#endif
