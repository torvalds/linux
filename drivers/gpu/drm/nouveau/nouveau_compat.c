#include "nouveau_drm.h"
#include "nouveau_compat.h"

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>
#include <subdev/clock.h>
#include <subdev/mc.h>

void *nouveau_newpriv(struct drm_device *);

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
	nv_subdev(pmc)->intr(pmc);
}
