#ifndef __NVKM_DEVICE_H__
#define __NVKM_DEVICE_H__
#include <core/event.h>
#include <core/object.h>

enum nvkm_devidx {
	NVKM_SUBDEV_PCI,
	NVKM_SUBDEV_VBIOS,
	NVKM_SUBDEV_DEVINIT,
	NVKM_SUBDEV_TOP,
	NVKM_SUBDEV_IBUS,
	NVKM_SUBDEV_GPIO,
	NVKM_SUBDEV_I2C,
	NVKM_SUBDEV_FUSE,
	NVKM_SUBDEV_MXM,
	NVKM_SUBDEV_MC,
	NVKM_SUBDEV_BUS,
	NVKM_SUBDEV_TIMER,
	NVKM_SUBDEV_INSTMEM,
	NVKM_SUBDEV_FB,
	NVKM_SUBDEV_LTC,
	NVKM_SUBDEV_MMU,
	NVKM_SUBDEV_BAR,
	NVKM_SUBDEV_PMU,
	NVKM_SUBDEV_VOLT,
	NVKM_SUBDEV_ICCSENSE,
	NVKM_SUBDEV_THERM,
	NVKM_SUBDEV_CLK,
	NVKM_SUBDEV_SECBOOT,

	NVKM_ENGINE_BSP,

	NVKM_ENGINE_CE0,
	NVKM_ENGINE_CE1,
	NVKM_ENGINE_CE2,
	NVKM_ENGINE_CE3,
	NVKM_ENGINE_CE4,
	NVKM_ENGINE_CE5,
	NVKM_ENGINE_CE_LAST = NVKM_ENGINE_CE5,

	NVKM_ENGINE_CIPHER,
	NVKM_ENGINE_DISP,
	NVKM_ENGINE_DMAOBJ,
	NVKM_ENGINE_FIFO,
	NVKM_ENGINE_GR,
	NVKM_ENGINE_IFB,
	NVKM_ENGINE_ME,
	NVKM_ENGINE_MPEG,
	NVKM_ENGINE_MSENC,
	NVKM_ENGINE_MSPDEC,
	NVKM_ENGINE_MSPPP,
	NVKM_ENGINE_MSVLD,

	NVKM_ENGINE_NVENC0,
	NVKM_ENGINE_NVENC1,
	NVKM_ENGINE_NVENC2,
	NVKM_ENGINE_NVENC_LAST = NVKM_ENGINE_NVENC2,

	NVKM_ENGINE_NVDEC,
	NVKM_ENGINE_PM,
	NVKM_ENGINE_SEC,
	NVKM_ENGINE_SW,
	NVKM_ENGINE_VIC,
	NVKM_ENGINE_VP,

	NVKM_SUBDEV_NR
};

enum nvkm_device_type {
	NVKM_DEVICE_PCI,
	NVKM_DEVICE_AGP,
	NVKM_DEVICE_PCIE,
	NVKM_DEVICE_TEGRA,
};

struct nvkm_device {
	const struct nvkm_device_func *func;
	const struct nvkm_device_quirk *quirk;
	struct device *dev;
	enum nvkm_device_type type;
	u64 handle;
	const char *name;
	const char *cfgopt;
	const char *dbgopt;

	struct list_head head;
	struct mutex mutex;
	int refcount;

	void __iomem *pri;

	struct nvkm_event event;

	u64 disable_mask;
	u32 debug;

	const struct nvkm_device_chip *chip;
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
		GP100    = 0x130,
	} card_type;
	u32 chipset;
	u8  chiprev;
	u32 crystal;

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
	struct nvkm_iccsense *iccsense;
	struct nvkm_instmem *imem;
	struct nvkm_ltc *ltc;
	struct nvkm_mc *mc;
	struct nvkm_mmu *mmu;
	struct nvkm_subdev *mxm;
	struct nvkm_pci *pci;
	struct nvkm_pmu *pmu;
	struct nvkm_secboot *secboot;
	struct nvkm_therm *therm;
	struct nvkm_timer *timer;
	struct nvkm_top *top;
	struct nvkm_volt *volt;

	struct nvkm_engine *bsp;
	struct nvkm_engine *ce[6];
	struct nvkm_engine *cipher;
	struct nvkm_disp *disp;
	struct nvkm_dma *dma;
	struct nvkm_fifo *fifo;
	struct nvkm_gr *gr;
	struct nvkm_engine *ifb;
	struct nvkm_engine *me;
	struct nvkm_engine *mpeg;
	struct nvkm_engine *msenc;
	struct nvkm_engine *mspdec;
	struct nvkm_engine *msppp;
	struct nvkm_engine *msvld;
	struct nvkm_engine *nvenc[3];
	struct nvkm_engine *nvdec;
	struct nvkm_pm *pm;
	struct nvkm_engine *sec;
	struct nvkm_sw *sw;
	struct nvkm_engine *vic;
	struct nvkm_engine *vp;
};

struct nvkm_subdev *nvkm_device_subdev(struct nvkm_device *, int index);
struct nvkm_engine *nvkm_device_engine(struct nvkm_device *, int index);

struct nvkm_device_func {
	struct nvkm_device_pci *(*pci)(struct nvkm_device *);
	struct nvkm_device_tegra *(*tegra)(struct nvkm_device *);
	void *(*dtor)(struct nvkm_device *);
	int (*preinit)(struct nvkm_device *);
	int (*init)(struct nvkm_device *);
	void (*fini)(struct nvkm_device *, bool suspend);
	resource_size_t (*resource_addr)(struct nvkm_device *, unsigned bar);
	resource_size_t (*resource_size)(struct nvkm_device *, unsigned bar);
	bool cpu_coherent;
};

struct nvkm_device_quirk {
	u8 tv_pin_mask;
	u8 tv_gpio;
};

struct nvkm_device_chip {
	const char *name;

	int (*bar     )(struct nvkm_device *, int idx, struct nvkm_bar **);
	int (*bios    )(struct nvkm_device *, int idx, struct nvkm_bios **);
	int (*bus     )(struct nvkm_device *, int idx, struct nvkm_bus **);
	int (*clk     )(struct nvkm_device *, int idx, struct nvkm_clk **);
	int (*devinit )(struct nvkm_device *, int idx, struct nvkm_devinit **);
	int (*fb      )(struct nvkm_device *, int idx, struct nvkm_fb **);
	int (*fuse    )(struct nvkm_device *, int idx, struct nvkm_fuse **);
	int (*gpio    )(struct nvkm_device *, int idx, struct nvkm_gpio **);
	int (*i2c     )(struct nvkm_device *, int idx, struct nvkm_i2c **);
	int (*ibus    )(struct nvkm_device *, int idx, struct nvkm_subdev **);
	int (*iccsense)(struct nvkm_device *, int idx, struct nvkm_iccsense **);
	int (*imem    )(struct nvkm_device *, int idx, struct nvkm_instmem **);
	int (*ltc     )(struct nvkm_device *, int idx, struct nvkm_ltc **);
	int (*mc      )(struct nvkm_device *, int idx, struct nvkm_mc **);
	int (*mmu     )(struct nvkm_device *, int idx, struct nvkm_mmu **);
	int (*mxm     )(struct nvkm_device *, int idx, struct nvkm_subdev **);
	int (*pci     )(struct nvkm_device *, int idx, struct nvkm_pci **);
	int (*pmu     )(struct nvkm_device *, int idx, struct nvkm_pmu **);
	int (*secboot )(struct nvkm_device *, int idx, struct nvkm_secboot **);
	int (*therm   )(struct nvkm_device *, int idx, struct nvkm_therm **);
	int (*timer   )(struct nvkm_device *, int idx, struct nvkm_timer **);
	int (*top     )(struct nvkm_device *, int idx, struct nvkm_top **);
	int (*volt    )(struct nvkm_device *, int idx, struct nvkm_volt **);

	int (*bsp     )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*ce[6]   )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*cipher  )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*disp    )(struct nvkm_device *, int idx, struct nvkm_disp **);
	int (*dma     )(struct nvkm_device *, int idx, struct nvkm_dma **);
	int (*fifo    )(struct nvkm_device *, int idx, struct nvkm_fifo **);
	int (*gr      )(struct nvkm_device *, int idx, struct nvkm_gr **);
	int (*ifb     )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*me      )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*mpeg    )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*msenc   )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*mspdec  )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*msppp   )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*msvld   )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*nvenc[3])(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*nvdec   )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*pm      )(struct nvkm_device *, int idx, struct nvkm_pm **);
	int (*sec     )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*sw      )(struct nvkm_device *, int idx, struct nvkm_sw **);
	int (*vic     )(struct nvkm_device *, int idx, struct nvkm_engine **);
	int (*vp      )(struct nvkm_device *, int idx, struct nvkm_engine **);
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

void nvkm_device_del(struct nvkm_device **);

struct nvkm_device_oclass {
	int (*ctor)(struct nvkm_device *, const struct nvkm_oclass *,
		    void *data, u32 size, struct nvkm_object **);
	struct nvkm_sclass base;
};

extern const struct nvkm_sclass nvkm_udevice_sclass;

/* device logging */
#define nvdev_printk_(d,l,p,f,a...) do {                                       \
	const struct nvkm_device *_device = (d);                               \
	if (_device->debug >= (l))                                             \
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
