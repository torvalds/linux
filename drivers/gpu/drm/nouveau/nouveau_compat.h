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

void nv_intr(struct drm_device *);

bool nouveau_wait_eq(struct drm_device *, uint64_t timeout,
			    uint32_t reg, uint32_t mask, uint32_t val);
bool nouveau_wait_ne(struct drm_device *, uint64_t timeout,
			    uint32_t reg, uint32_t mask, uint32_t val);
bool nouveau_wait_cb(struct drm_device *, u64 timeout,
			    bool (*cond)(void *), void *);

u64 nv_timer_read(struct drm_device *);

int  nvfb_tile_nr(struct drm_device *);
void nvfb_tile_init(struct drm_device *, int, u32, u32, u32, u32);
void nvfb_tile_fini(struct drm_device *, int);
void nvfb_tile_prog(struct drm_device *, int);

struct nouveau_fb_tile *nvfb_tile(struct drm_device *, int);

struct nouveau_mem;
int nvfb_vram_get(struct drm_device *dev, u64 size, u32 align, u32 ncmin,
		  u32 memtype, struct nouveau_mem **pmem);
void nvfb_vram_put(struct drm_device *dev, struct nouveau_mem **pmem);
bool nvfb_flags_valid(struct drm_device *dev, u32);

u64 nvfb_vram_sys_base(struct drm_device *);
u64 nvfb_vram_size(struct drm_device *);
int nvfb_vram_type(struct drm_device *);
int nvfb_vram_rank_B(struct drm_device *);

void nv50_fb_vm_trap(struct drm_device *, int);

struct nouveau_gpuobj *nvimem_ramro(struct drm_device *);
struct nouveau_gpuobj *nvimem_ramfc(struct drm_device *);

int _nouveau_gpuobj_new(struct drm_device *dev, struct nouveau_gpuobj *par,
			int size, int align, u32 flags,
			struct nouveau_gpuobj **pboj);

u32 nv_ri32(struct drm_device *, u32);
void nv_wi32(struct drm_device *, u32, u32);
u32 nvimem_reserved(struct drm_device *);

void nvimem_flush(struct drm_device *);

void _nv50_vm_flush_engine(struct drm_device *dev, int engine);

int _nouveau_vm_new(struct drm_device *, u64 offset, u64 length,
		    u64 mm_offset, struct nouveau_vm **);

struct nouveau_vma;
int nouveau_gpuobj_map_bar(struct nouveau_gpuobj *, u32, struct nouveau_vma *);

int
nvbar_map(struct drm_device *dev, struct nouveau_mem *mem, u32 flags,
	  struct nouveau_vma *vma);
void
nvbar_unmap(struct drm_device *dev, struct nouveau_vma *vma);

struct nouveau_vm *
nv04vm_ref(struct drm_device *dev);

struct nouveau_gpuobj *
nv04vm_refdma(struct drm_device *dev);

void
nvvm_engref(struct nouveau_vm *, int, int);

int
nvvm_spg_shift(struct nouveau_vm *);

int
nvvm_lpg_shift(struct nouveau_vm *);

#endif
