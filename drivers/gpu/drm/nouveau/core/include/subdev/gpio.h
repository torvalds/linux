#ifndef __NOUVEAU_GPIO_H__
#define __NOUVEAU_GPIO_H__

#include <core/subdev.h>
#include <core/device.h>

#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

struct nouveau_gpio {
	struct nouveau_subdev base;

	/* hardware interfaces */
	void (*reset)(struct nouveau_gpio *, u8 func);
	int  (*drive)(struct nouveau_gpio *, int line, int dir, int out);
	int  (*sense)(struct nouveau_gpio *, int line);
	void (*irq_enable)(struct nouveau_gpio *, int line, bool);

	/* software interfaces */
	int  (*find)(struct nouveau_gpio *, int idx, u8 tag, u8 line,
		     struct dcb_gpio_func *);
	int  (*set)(struct nouveau_gpio *, int idx, u8 tag, u8 line, int state);
	int  (*get)(struct nouveau_gpio *, int idx, u8 tag, u8 line);
	int  (*irq)(struct nouveau_gpio *, int idx, u8 tag, u8 line, bool on);

	/* interrupt handling */
	struct list_head isr;
	spinlock_t lock;

	void (*isr_run)(struct nouveau_gpio *, int idx, u32 mask);
	int  (*isr_add)(struct nouveau_gpio *, int idx, u8 tag, u8 line,
			void (*)(void *, int state), void *data);
	void (*isr_del)(struct nouveau_gpio *, int idx, u8 tag, u8 line,
			void (*)(void *, int state), void *data);
};

static inline struct nouveau_gpio *
nouveau_gpio(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_GPIO];
}

#define nouveau_gpio_create(p,e,o,d)                                           \
	nouveau_gpio_create_((p), (e), (o), sizeof(**d), (void **)d)
#define nouveau_gpio_destroy(p)                                                \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_gpio_fini(p,s)                                                 \
	nouveau_subdev_fini(&(p)->base, (s))

int nouveau_gpio_create_(struct nouveau_object *, struct nouveau_object *,
			 struct nouveau_oclass *, int, void **);
int nouveau_gpio_init(struct nouveau_gpio *);

extern struct nouveau_oclass nv10_gpio_oclass;
extern struct nouveau_oclass nv50_gpio_oclass;
extern struct nouveau_oclass nvd0_gpio_oclass;

void nv50_gpio_dtor(struct nouveau_object *);
int  nv50_gpio_init(struct nouveau_object *);
int  nv50_gpio_fini(struct nouveau_object *, bool);
void nv50_gpio_intr(struct nouveau_subdev *);
void nv50_gpio_irq_enable(struct nouveau_gpio *, int line, bool);

#endif
