#ifndef __NVKM_DEVICE_H__
#define __NVKM_DEVICE_H__
#include <core/engine.h>
#include <core/event.h>

enum nvkm_devidx {
	NVDEV_ENGINE_DEVICE,
	NVDEV_SUBDEV_VBIOS,

	/* All subdevs from DEVINIT to DEVINIT_LAST will be created before
	 * *any* of them are initialised.  This subdev category is used
	 * for any subdevs that the VBIOS init table parsing may call out
	 * to during POST.
	 */
	NVDEV_SUBDEV_DEVINIT,
	NVDEV_SUBDEV_IBUS,
	NVDEV_SUBDEV_GPIO,
	NVDEV_SUBDEV_I2C,
	NVDEV_SUBDEV_DEVINIT_LAST = NVDEV_SUBDEV_I2C,

	/* This grouping of subdevs are initialised right after they've
	 * been created, and are allowed to assume any subdevs in the
	 * list above them exist and have been initialised.
	 */
	NVDEV_SUBDEV_FUSE,
	NVDEV_SUBDEV_MXM,
	NVDEV_SUBDEV_MC,
	NVDEV_SUBDEV_BUS,
	NVDEV_SUBDEV_TIMER,
	NVDEV_SUBDEV_FB,
	NVDEV_SUBDEV_LTC,
	NVDEV_SUBDEV_INSTMEM,
	NVDEV_SUBDEV_MMU,
	NVDEV_SUBDEV_BAR,
	NVDEV_SUBDEV_PMU,
	NVDEV_SUBDEV_VOLT,
	NVDEV_SUBDEV_THERM,
	NVDEV_SUBDEV_CLK,

	NVDEV_ENGINE_FIRST,
	NVDEV_ENGINE_DMAOBJ = NVDEV_ENGINE_FIRST,
	NVDEV_ENGINE_IFB,
	NVDEV_ENGINE_FIFO,
	NVDEV_ENGINE_SW,
	NVDEV_ENGINE_GR,
	NVDEV_ENGINE_MPEG,
	NVDEV_ENGINE_ME,
	NVDEV_ENGINE_VP,
	NVDEV_ENGINE_CIPHER,
	NVDEV_ENGINE_BSP,
	NVDEV_ENGINE_MSPPP,
	NVDEV_ENGINE_CE0,
	NVDEV_ENGINE_CE1,
	NVDEV_ENGINE_CE2,
	NVDEV_ENGINE_VIC,
	NVDEV_ENGINE_MSENC,
	NVDEV_ENGINE_DISP,
	NVDEV_ENGINE_PM,
	NVDEV_ENGINE_MSVLD,
	NVDEV_ENGINE_SEC,
	NVDEV_ENGINE_MSPDEC,

	NVDEV_SUBDEV_NR,
};

struct nvkm_device {
	struct nvkm_engine engine;
	struct list_head head;

	struct pci_dev *pdev;
	struct platform_device *platformdev;
	struct device *dev;
	u64 handle;

	void __iomem *pri;

	struct nvkm_event event;

	const char *cfgopt;
	const char *dbgopt;
	const char *name;
	const char *cname;
	u64 disable_mask;

	enum {
		NV_04    = 0x04,
		NV_10    = 0x10,
		NV_11    = 0x11,
		NV_20    = 0x20,
		NV_30    = 0x30,
		NV_40    = 0x40,
		NV_50    = 0x50,
		NV_C0    = 0xc0,
		NV_E0    = 0xe0,
		GM100    = 0x110,
	} card_type;
	u32 chipset;
	u8  chiprev;
	u32 crystal;

	struct nvkm_oclass *oclass[NVDEV_SUBDEV_NR];
	struct nvkm_object *subdev[NVDEV_SUBDEV_NR];

	struct {
		struct notifier_block nb;
	} acpi;

	struct nvkm_bar *bar;
	struct nvkm_bios *bios;
	struct nvkm_bus *bus;
	struct nvkm_clk *clk;
	struct nvkm_devinit *devinit;
	struct nvkm_fb *fb;
	struct nvkm_fuse *fuse;
	struct nvkm_gpio *gpio;
	struct nvkm_i2c *i2c;
	struct nvkm_subdev *ibus;
	struct nvkm_instmem *imem;
	struct nvkm_ltc *ltc;
	struct nvkm_mc *mc;
	struct nvkm_mmu *mmu;
	struct nvkm_subdev *mxm;
	struct nvkm_pmu *pmu;
	struct nvkm_therm *therm;
	struct nvkm_timer *timer;
	struct nvkm_volt *volt;

	struct nvkm_engine *bsp;
	struct nvkm_engine *ce[3];
	struct nvkm_engine *cipher;
	struct nvkm_disp *disp;
	struct nvkm_dmaeng *dma;
	struct nvkm_fifo *fifo;
	struct nvkm_gr *gr;
	struct nvkm_engine *ifb;
	struct nvkm_engine *me;
	struct nvkm_engine *mpeg;
	struct nvkm_engine *msenc;
	struct nvkm_engine *mspdec;
	struct nvkm_engine *msppp;
	struct nvkm_engine *msvld;
	struct nvkm_pm *pm;
	struct nvkm_engine *sec;
	struct nvkm_sw *sw;
	struct nvkm_engine *vic;
	struct nvkm_engine *vp;

	struct nouveau_platform_gpu *gpu;
};

struct nvkm_device *nvkm_device_find(u64 name);
int nvkm_device_list(u64 *name, int size);

/* privileged register interface accessor macros */
#define nvkm_rd08(d,a) ioread8((d)->pri + (a))
#define nvkm_rd16(d,a) ioread16_native((d)->pri + (a))
#define nvkm_rd32(d,a) ioread32_native((d)->pri + (a))
#define nvkm_wr08(d,a,v) iowrite8((v), (d)->pri + (a))
#define nvkm_wr16(d,a,v) iowrite16_native((v), (d)->pri + (a))
#define nvkm_wr32(d,a,v) iowrite32_native((v), (d)->pri + (a))
#define nvkm_mask(d,a,m,v) ({                                                  \
	struct nvkm_device *_device = (d);                                     \
	u32 _addr = (a), _temp = nvkm_rd32(_device, _addr);                    \
	nvkm_wr32(_device, _addr, (_temp & ~(m)) | (v));                       \
	_temp;                                                                 \
})

struct nvkm_device *nv_device(void *obj);

static inline bool
nv_device_match(struct nvkm_device *device, u16 dev, u16 ven, u16 sub)
{
	return device->pdev->device == dev &&
	       device->pdev->subsystem_vendor == ven &&
	       device->pdev->subsystem_device == sub;
}

static inline bool
nv_device_is_pci(struct nvkm_device *device)
{
	return device->pdev != NULL;
}

static inline bool
nv_device_is_cpu_coherent(struct nvkm_device *device)
{
	return (!IS_ENABLED(CONFIG_ARM) && nv_device_is_pci(device));
}

static inline struct device *
nv_device_base(struct nvkm_device *device)
{
	return nv_device_is_pci(device) ? &device->pdev->dev :
					  &device->platformdev->dev;
}

resource_size_t
nv_device_resource_start(struct nvkm_device *device, unsigned int bar);

resource_size_t
nv_device_resource_len(struct nvkm_device *device, unsigned int bar);

int
nv_device_get_irq(struct nvkm_device *device, bool stall);

struct platform_device;

enum nv_bus_type {
	NVKM_BUS_PCI,
	NVKM_BUS_PLATFORM,
};

int  nvkm_device_new(void *, enum nv_bus_type type, u64 name,
		     const char *sname, const char *cfg, const char *dbg,
		     struct nvkm_device **);
void nvkm_device_del(struct nvkm_device **);

/* device logging */
#define nvdev_printk_(d,l,p,f,a...) do {                                       \
	struct nvkm_device *_device = (d);                                     \
	if (_device->engine.subdev.debug >= (l))                               \
		dev_##p(_device->dev, f, ##a);                                 \
} while(0)
#define nvdev_printk(d,l,p,f,a...) nvdev_printk_((d), NV_DBG_##l, p, f, ##a)
#define nvdev_fatal(d,f,a...) nvdev_printk((d), FATAL,   crit, f, ##a)
#define nvdev_error(d,f,a...) nvdev_printk((d), ERROR,    err, f, ##a)
#define nvdev_warn(d,f,a...)  nvdev_printk((d),  WARN, notice, f, ##a)
#define nvdev_info(d,f,a...)  nvdev_printk((d),  INFO,   info, f, ##a)
#define nvdev_debug(d,f,a...) nvdev_printk((d), DEBUG,   info, f, ##a)
#define nvdev_trace(d,f,a...) nvdev_printk((d), TRACE,   info, f, ##a)
#define nvdev_spam(d,f,a...)  nvdev_printk((d),  SPAM,    dbg, f, ##a)
#endif
