/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMAC_PFUNC_H__
#define __PMAC_PFUNC_H__

#include <linux/types.h>
#include <linux/list.h>

/* Flags in command lists */
#define PMF_FLAGS_ON_INIT		0x80000000u
#define PMF_FLGAS_ON_TERM		0x40000000u
#define PMF_FLAGS_ON_SLEEP		0x20000000u
#define PMF_FLAGS_ON_WAKE		0x10000000u
#define PMF_FLAGS_ON_DEMAND		0x08000000u
#define PMF_FLAGS_INT_GEN		0x04000000u
#define PMF_FLAGS_HIGH_SPEED		0x02000000u
#define PMF_FLAGS_LOW_SPEED		0x01000000u
#define PMF_FLAGS_SIDE_EFFECTS		0x00800000u

/*
 * Arguments to a platform function call.
 *
 * NOTE: By convention, pointer arguments point to an u32
 */
struct pmf_args {
	union {
		u32 v;
		u32 *p;
	} u[4];
	unsigned int count;
};

/*
 * A driver capable of interpreting commands provides a handlers
 * structure filled with whatever handlers are implemented by this
 * driver. Non implemented handlers are left NULL.
 *
 * PMF_STD_ARGS are the same arguments that are passed to the parser
 * and that gets passed back to the various handlers.
 *
 * Interpreting a given function always start with a begin() call which
 * returns an instance data to be passed around subsequent calls, and
 * ends with an end() call. This allows the low level driver to implement
 * locking policy or per-function instance data.
 *
 * For interrupt capable functions, irq_enable() is called when a client
 * registers, and irq_disable() is called when the last client unregisters
 * Note that irq_enable & irq_disable are called within a semaphore held
 * by the core, thus you should not try to register yourself to some other
 * pmf interrupt during those calls.
 */

#define PMF_STD_ARGS	struct pmf_function *func, void *instdata, \
		        struct pmf_args *args

struct pmf_function;

struct pmf_handlers {
	void * (*begin)(struct pmf_function *func, struct pmf_args *args);
	void (*end)(struct pmf_function *func, void *instdata);

	int (*irq_enable)(struct pmf_function *func);
	int (*irq_disable)(struct pmf_function *func);

	int (*write_gpio)(PMF_STD_ARGS, u8 value, u8 mask);
	int (*read_gpio)(PMF_STD_ARGS, u8 mask, int rshift, u8 xor);

	int (*write_reg32)(PMF_STD_ARGS, u32 offset, u32 value, u32 mask);
	int (*read_reg32)(PMF_STD_ARGS, u32 offset);
	int (*write_reg16)(PMF_STD_ARGS, u32 offset, u16 value, u16 mask);
	int (*read_reg16)(PMF_STD_ARGS, u32 offset);
	int (*write_reg8)(PMF_STD_ARGS, u32 offset, u8 value, u8 mask);
	int (*read_reg8)(PMF_STD_ARGS, u32 offset);

	int (*delay)(PMF_STD_ARGS, u32 duration);

	int (*wait_reg32)(PMF_STD_ARGS, u32 offset, u32 value, u32 mask);
	int (*wait_reg16)(PMF_STD_ARGS, u32 offset, u16 value, u16 mask);
	int (*wait_reg8)(PMF_STD_ARGS, u32 offset, u8 value, u8 mask);

	int (*read_i2c)(PMF_STD_ARGS, u32 len);
	int (*write_i2c)(PMF_STD_ARGS, u32 len, const u8 *data);
	int (*rmw_i2c)(PMF_STD_ARGS, u32 masklen, u32 valuelen, u32 totallen,
		       const u8 *maskdata, const u8 *valuedata);

	int (*read_cfg)(PMF_STD_ARGS, u32 offset, u32 len);
	int (*write_cfg)(PMF_STD_ARGS, u32 offset, u32 len, const u8 *data);
	int (*rmw_cfg)(PMF_STD_ARGS, u32 offset, u32 masklen, u32 valuelen,
		       u32 totallen, const u8 *maskdata, const u8 *valuedata);

	int (*read_i2c_sub)(PMF_STD_ARGS, u8 subaddr, u32 len);
	int (*write_i2c_sub)(PMF_STD_ARGS, u8 subaddr, u32 len, const u8 *data);
	int (*set_i2c_mode)(PMF_STD_ARGS, int mode);
	int (*rmw_i2c_sub)(PMF_STD_ARGS, u8 subaddr, u32 masklen, u32 valuelen,
			   u32 totallen, const u8 *maskdata,
			   const u8 *valuedata);

	int (*read_reg32_msrx)(PMF_STD_ARGS, u32 offset, u32 mask, u32 shift,
			       u32 xor);
	int (*read_reg16_msrx)(PMF_STD_ARGS, u32 offset, u32 mask, u32 shift,
			       u32 xor);
	int (*read_reg8_msrx)(PMF_STD_ARGS, u32 offset, u32 mask, u32 shift,
			      u32 xor);

	int (*write_reg32_slm)(PMF_STD_ARGS, u32 offset, u32 shift, u32 mask);
	int (*write_reg16_slm)(PMF_STD_ARGS, u32 offset, u32 shift, u32 mask);
	int (*write_reg8_slm)(PMF_STD_ARGS, u32 offset, u32 shift, u32 mask);

	int (*mask_and_compare)(PMF_STD_ARGS, u32 len, const u8 *maskdata,
				const u8 *valuedata);

	struct module *owner;
};


/*
 * Drivers who expose platform functions register at init time, this
 * causes the platform functions for that device node to be parsed in
 * advance and associated with the device. The data structures are
 * partially public so a driver can walk the list of platform functions
 * and eventually inspect the flags
 */
struct pmf_device;

struct pmf_function {
	/* All functions for a given driver are linked */
	struct list_head	link;

	/* Function node & driver data */
	struct device_node	*node;
	void			*driver_data;

	/* For internal use by core */
	struct pmf_device	*dev;

	/* The name is the "xxx" in "platform-do-xxx", this is how
	 * platform functions are identified by this code. Some functions
	 * only operate for a given target, in which case the phandle is
	 * here (or 0 if the filter doesn't apply)
	 */
	const char		*name;
	u32			phandle;

	/* The flags for that function. You can have several functions
	 * with the same name and different flag
	 */
	u32			flags;

	/* The actual tokenized function blob */
	const void		*data;
	unsigned int		length;

	/* Interrupt clients */
	struct list_head	irq_clients;

	/* Refcounting */
	struct kref		ref;
};

/*
 * For platform functions that are interrupts, one can register
 * irq_client structures. You canNOT use the same structure twice
 * as it contains a link member. Also, the callback is called with
 * a spinlock held, you must not call back into any of the pmf_* functions
 * from within that callback
 */
struct pmf_irq_client {
	void			(*handler)(void *data);
	void			*data;
	struct module		*owner;
	struct list_head	link;
	struct pmf_function	*func;
};


/*
 * Register/Unregister a function-capable driver and its handlers
 */
extern int pmf_register_driver(struct device_node *np,
			      struct pmf_handlers *handlers,
			      void *driverdata);

extern void pmf_unregister_driver(struct device_node *np);


/*
 * Register/Unregister interrupt clients
 */
extern int pmf_register_irq_client(struct device_node *np,
				   const char *name,
				   struct pmf_irq_client *client);

extern void pmf_unregister_irq_client(struct pmf_irq_client *client);

/*
 * Called by the handlers when an irq happens
 */
extern void pmf_do_irq(struct pmf_function *func);


/*
 * Low level call to platform functions.
 *
 * The phandle can filter on the target object for functions that have
 * multiple targets, the flags allow you to restrict the call to a given
 * combination of flags.
 *
 * The args array contains as many arguments as is required by the function,
 * this is dependent on the function you are calling, unfortunately Apple
 * mechanism provides no way to encode that so you have to get it right at
 * the call site. Some functions require no args, in which case, you can
 * pass NULL.
 *
 * You can also pass NULL to the name. This will match any function that has
 * the appropriate combination of flags & phandle or you can pass 0 to the
 * phandle to match any
 */
extern int pmf_do_functions(struct device_node *np, const char *name,
			    u32 phandle, u32 flags, struct pmf_args *args);



/*
 * High level call to a platform function.
 *
 * This one looks for the platform-xxx first so you should call it to the
 * actual target if any. It will fallback to platform-do-xxx if it can't
 * find one. It will also exclusively target functions that have
 * the "OnDemand" flag.
 */

extern int pmf_call_function(struct device_node *target, const char *name,
			     struct pmf_args *args);


/*
 * For low latency interrupt usage, you can lookup for on-demand functions
 * using the functions below
 */

extern struct pmf_function *pmf_find_function(struct device_node *target,
					      const char *name);

extern struct pmf_function * pmf_get_function(struct pmf_function *func);
extern void pmf_put_function(struct pmf_function *func);

extern int pmf_call_one(struct pmf_function *func, struct pmf_args *args);


/* Suspend/resume code called by via-pmu directly for now */
extern void pmac_pfunc_base_suspend(void);
extern void pmac_pfunc_base_resume(void);

#endif /* __PMAC_PFUNC_H__ */
