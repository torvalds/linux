#include "nouveau_drm.h"
#include "nouveau_chan.h"
#include "nouveau_compat.h"

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/clock.h>
#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/bar.h>
#include <subdev/vm.h>

int
nvdrm_gart_init(struct drm_device *dev, u64 *base, u64 *size)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	if (drm->agp.stat == ENABLED) {
		*base = drm->agp.base;
		*size = drm->agp.base;
		return 0;
	}
	return -ENODEV;
}

u8
_nv_rd08(struct drm_device *dev, u32 reg)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nv_ro08(drm->device, reg);
}

void
_nv_wr08(struct drm_device *dev, u32 reg, u8 val)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	nv_wo08(drm->device, reg, val);
}

u32
_nv_rd32(struct drm_device *dev, u32 reg)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nv_ro32(drm->device, reg);
}

void
_nv_wr32(struct drm_device *dev, u32 reg, u32 val)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	nv_wo32(drm->device, reg, val);
}

u32
_nv_mask(struct drm_device *dev, u32 reg, u32 mask, u32 val)
{
	u32 tmp = _nv_rd32(dev, reg);
	_nv_wr32(dev, reg, (tmp & ~mask) | val);
	return tmp;
}

bool
_nv_bios(struct drm_device *dev, u8 **data, u32 *size)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_bios *bios = nouveau_bios(drm->device);
	*data = bios->data;
	*size = bios->size;
	return true;
}

void
nouveau_gpio_reset(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	gpio->reset(gpio);
}

int
nouveau_gpio_find(struct drm_device *dev, int idx, u8 tag, u8 line,
		  struct dcb_gpio_func *func)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);

	return gpio->find(gpio, idx, tag, line, func);
}

bool
nouveau_gpio_func_valid(struct drm_device *dev, u8 tag)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	struct dcb_gpio_func func;

	return gpio->find(gpio, 0, tag, 0xff, &func) == 0;
}

int
nouveau_gpio_func_set(struct drm_device *dev, u8 tag, int state)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	if (gpio && gpio->get)
		return gpio->set(gpio, 0, tag, 0xff, state);
	return -ENODEV;
}

int
nouveau_gpio_func_get(struct drm_device *dev, u8 tag)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	if (gpio && gpio->get)
		return gpio->get(gpio, 0, tag, 0xff);
	return -ENODEV;
}

int
nouveau_gpio_irq(struct drm_device *dev, int idx, u8 tag, u8 line, bool on)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	if (gpio && gpio->irq)
		return gpio->irq(gpio, idx, tag, line, on);
	return -ENODEV;
}

int
nouveau_gpio_isr_add(struct drm_device *dev, int idx, u8 tag, u8 line,
		     void (*exec)(void *, int state), void *data)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	if (gpio && gpio->isr_add)
		return gpio->isr_add(gpio, idx, tag, line, exec, data);
	return -ENODEV;
}

void
nouveau_gpio_isr_del(struct drm_device *dev, int idx, u8 tag, u8 line,
		     void (*exec)(void *, int state), void *data)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	if (gpio && gpio->isr_del)
		gpio->isr_del(gpio, idx, tag, line, exec, data);
}

struct nouveau_i2c_port *
nouveau_i2c_find(struct drm_device *dev, u8 index)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);

	return i2c->find(i2c, index);
}

bool
nouveau_probe_i2c_addr(struct nouveau_i2c_port *port, int addr)
{
	return nv_probe_i2c(port, addr);
}

struct i2c_adapter *
nouveau_i2c_adapter(struct nouveau_i2c_port *port)
{
	return &port->adapter;
}


int
nouveau_i2c_identify(struct drm_device *dev, const char *what,
		     struct i2c_board_info *info,
		     bool (*match)(struct nouveau_i2c_port *,
			           struct i2c_board_info *),
		     int index)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_i2c *i2c = nouveau_i2c(drm->device);

	return i2c->identify(i2c, index, what, info, match);
}

int
auxch_rd(struct drm_device *dev, struct nouveau_i2c_port *port,
	 u32 addr, u8 *data, u8 size)
{
	return nv_rdaux(port, addr, data, size);
}

int
auxch_wr(struct drm_device *dev, struct nouveau_i2c_port *port,
	 u32 addr, u8 *data, u8 size)
{
	return nv_wraux(port, addr, data, size);
}

u32
get_pll_register(struct drm_device *dev, u32 type)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_bios *bios = nouveau_bios(drm->device);
	struct nvbios_pll info;

	if (nvbios_pll_parse(bios, type, &info))
		return 0;
	return info.reg;
}

int
get_pll_limits(struct drm_device *dev, u32 type, struct nvbios_pll *info)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_bios *bios = nouveau_bios(drm->device);

	return nvbios_pll_parse(bios, type, info);
}

int
setPLL(struct drm_device *dev, u32 reg, u32 freq)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_clock *clk = nouveau_clock(drm->device);
	int ret = -ENODEV;

	if (clk->pll_set)
		ret = clk->pll_set(clk, reg, freq);
	return ret;
}


int
nouveau_calc_pll_mnp(struct drm_device *dev, struct nvbios_pll *info,
		     int freq, struct nouveau_pll_vals *pv)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_clock *clk = nouveau_clock(drm->device);
	int ret = 0;

	if (clk->pll_calc)
		ret = clk->pll_calc(clk, info, freq, pv);
	return ret;
}

int
nouveau_hw_setpll(struct drm_device *dev, u32 reg1,
		  struct nouveau_pll_vals *pv)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_clock *clk = nouveau_clock(drm->device);
	int ret = -ENODEV;

	if (clk->pll_prog)
		ret = clk->pll_prog(clk, reg1, pv);
	return ret;
}

int nva3_pll_calc(struct nouveau_clock *, struct nvbios_pll *, u32 freq,
		  int *N, int *fN, int *M, int *P);

int
nva3_calc_pll(struct drm_device *dev, struct nvbios_pll *info, u32 freq,
	      int *N, int *fN, int *M, int *P)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_clock *clk = nouveau_clock(drm->device);

	return nva3_pll_calc(clk, info, freq, N, fN, M, P);
}

void
nouveau_bios_run_init_table(struct drm_device *dev, uint16_t table,
			    struct dcb_output *dcbent, int crtc)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_bios *bios = nouveau_bios(drm->device);
	struct nvbios_init init = {
		.subdev = nv_subdev(bios),
		.bios = bios,
		.offset = table,
		.outp = dcbent,
		.crtc = crtc,
		.execute = 1
	};

	nvbios_exec(&init);
}

void
nouveau_bios_init_exec(struct drm_device *dev, uint16_t table)
{
	nouveau_bios_run_init_table(dev, table, NULL, 0);
}

void
nv_intr(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_mc *pmc = nouveau_mc(drm->device);
	nv_subdev(pmc)->intr(&pmc->base);
}

bool nouveau_wait_eq(struct drm_device *dev, uint64_t timeout,
			    uint32_t reg, uint32_t mask, uint32_t val)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nouveau_timer_wait_eq(drm->device, timeout, reg, mask, val);
}

bool nouveau_wait_ne(struct drm_device *dev, uint64_t timeout,
			    uint32_t reg, uint32_t mask, uint32_t val)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nouveau_timer_wait_ne(drm->device, timeout, reg, mask, val);
}

bool nouveau_wait_cb(struct drm_device *dev, u64 timeout,
			    bool (*cond)(void *), void *data)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nouveau_timer_wait_cb(drm->device, timeout, cond, data);
}

u64
nv_timer_read(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_timer *ptimer = nouveau_timer(drm->device);
	return ptimer->read(ptimer);
}

int
nvfb_tile_nr(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return pfb->tile.regions;
}

struct nouveau_fb_tile *
nvfb_tile(struct drm_device *dev, int i)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return &pfb->tile.region[i];
}

void
nvfb_tile_init(struct drm_device *dev, int i, u32 a, u32 b, u32 c, u32 d)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	pfb->tile.init(pfb, i, a, b, c, d, &pfb->tile.region[i]);
}

void
nvfb_tile_fini(struct drm_device *dev, int i)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	pfb->tile.fini(pfb, i, &pfb->tile.region[i]);
}

void
nvfb_tile_prog(struct drm_device *dev, int i)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	pfb->tile.prog(pfb, i, &pfb->tile.region[i]);
}

bool
nvfb_flags_valid(struct drm_device *dev, u32 flags)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return pfb->memtype_valid(pfb, flags);
}

int
nvfb_vram_get(struct drm_device *dev, u64 size, u32 align, u32 ncmin,
	      u32 memtype, struct nouveau_mem **pmem)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	int ret = pfb->ram.get(pfb, size, align, ncmin, memtype, pmem);
	if (ret)
		return ret;
	(*pmem)->dev = dev;
	return 0;
}

void
nvfb_vram_put(struct drm_device *dev, struct nouveau_mem **pmem)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	pfb->ram.put(pfb, pmem);
}


u64 nvfb_vram_sys_base(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return pfb->ram.stolen;
}

u64 nvfb_vram_size(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return pfb->ram.size;
}

int nvfb_vram_type(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return pfb->ram.type;
}

int nvfb_vram_rank_B(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_fb *pfb = nouveau_fb(drm->device);
	return pfb->ram.ranks > 1;
}

void
nv50_fb_vm_trap(struct drm_device *dev, int disp)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	nv50_fb_trap(nouveau_fb(drm->device), disp);
}

#include <core/subdev/instmem/nv04.h>

struct nouveau_gpuobj *
nvimem_ramro(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nv04_instmem_priv *imem = (void *)nouveau_instmem(drm->device);
	return imem->ramro;
}

struct nouveau_gpuobj *
nvimem_ramfc(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nv04_instmem_priv *imem = (void *)nouveau_instmem(drm->device);
	return imem->ramfc;
}

int _nouveau_gpuobj_new(struct drm_device *dev, struct nouveau_gpuobj *par,
			int size, int align, u32 flags,
			struct nouveau_gpuobj **pobj)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	int ret;

	if (!par)
		flags |= NVOBJ_FLAG_HEAP;

	ret = nouveau_gpuobj_new(drm->device, nv_object(par), size, align,
				 flags, pobj);
	if (ret)
		return ret;

	(*pobj)->dev = dev;
	return 0;
}

u32 nv_ri32(struct drm_device *dev , u32 addr)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_instmem *imem = nouveau_instmem(drm->device);
	return nv_ro32(imem, addr);
}

void nv_wi32(struct drm_device *dev, u32 addr, u32 data)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_instmem *imem = nouveau_instmem(drm->device);
	nv_wo32(imem, addr, data);
}

u32 nvimem_reserved(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_instmem *imem = nouveau_instmem(drm->device);
	return imem->reserved;
}

int
nvbar_map(struct drm_device *dev, struct nouveau_mem *mem, u32 flags,
	  struct nouveau_vma *vma)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_bar *bar = nouveau_bar(drm->device);
	return bar->umap(bar, mem, flags, vma);
}

void
nvbar_unmap(struct drm_device *dev, struct nouveau_vma *vma)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_bar *bar = nouveau_bar(drm->device);
	bar->unmap(bar, vma);
}

int
nouveau_gpuobj_map_bar(struct nouveau_gpuobj *gpuobj, u32 flags,
		       struct nouveau_vma *vma)
{
	struct nouveau_drm *drm = nouveau_newpriv(gpuobj->dev);
	struct nouveau_bar *bar = nouveau_bar(drm->device);
	struct nouveau_instobj *iobj = (void *)
		nv_pclass(nv_object(gpuobj), NV_MEMOBJ_CLASS);
	struct nouveau_mem **mem = (void *)(iobj + 1);
	struct nouveau_mem *node = *mem;

	return bar->umap(bar, node, flags, vma);
}

void
nvimem_flush(struct drm_device *dev)
{
}

void _nv50_vm_flush_engine(struct drm_device *dev, int engine)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	nv50_vm_flush_engine(nv_subdev(drm->device), engine);
}

int _nouveau_vm_new(struct drm_device *dev, u64 offset, u64 length,
		    u64 mm_offset, struct nouveau_vm **pvm)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return nouveau_vm_new(nv_device(drm->device), offset, length, mm_offset, pvm);
}

#include <core/subdev/vm/nv04.h>
struct nouveau_vm *
nv04vm_ref(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	struct nouveau_vmmgr *vmm = nouveau_vmmgr(drm->device);
	struct nv04_vmmgr_priv *priv = (void *)vmm;
	return priv->vm;
}

struct nouveau_gpuobj *
nv04vm_refdma(struct drm_device *dev)
{
	struct nouveau_gpuobj *gpuobj = NULL;
	nouveau_gpuobj_ref(nv04vm_ref(dev)->pgt[0].obj[0], &gpuobj);
	return gpuobj;
}

void
nvvm_engref(struct nouveau_vm *vm, int eng, int ref)
{
	atomic_add(ref, &vm->engref[eng]);
}

int
nvvm_spg_shift(struct nouveau_vm *vm)
{
	return vm->vmm->spg_shift;
}

int
nvvm_lpg_shift(struct nouveau_vm *vm)
{
	return vm->vmm->lpg_shift;
}

u64 nvgpuobj_addr(struct nouveau_object *object)
{
	return nv_gpuobj(object)->addr;
}

struct drm_device *
nouveau_drv(void *ptr)
{
	struct nouveau_drm *drm = ptr;
	return drm->dev;
}

struct nouveau_channel *
nvdrm_channel(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_newpriv(dev);
	return drm->channel;
}

struct mutex *
nvchan_mutex(struct nouveau_channel *chan)
{
	return &chan->cli->mutex;
}
