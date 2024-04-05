/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BBC_I2C_H
#define _BBC_I2C_H

#include <linux/of.h>
#include <linux/list.h>

struct bbc_i2c_client {
	struct bbc_i2c_bus		*bp;
	struct platform_device		*op;
	int				bus;
	int				address;
};

enum fan_action { FAN_SLOWER, FAN_SAME, FAN_FASTER, FAN_FULLBLAST, FAN_STATE_MAX };

struct bbc_cpu_temperature {
	struct list_head		bp_list;
	struct list_head		glob_list;

	struct bbc_i2c_client		*client;
	int				index;

	/* Current readings, and history. */
	s8				curr_cpu_temp;
	s8				curr_amb_temp;
	s8				prev_cpu_temp;
	s8				prev_amb_temp;
	s8				avg_cpu_temp;
	s8				avg_amb_temp;

	int				sample_tick;

	enum fan_action			fan_todo[2];
#define FAN_AMBIENT	0
#define FAN_CPU		1
};

struct bbc_fan_control {
	struct list_head		bp_list;
	struct list_head		glob_list;

	struct bbc_i2c_client 		*client;
	int 				index;

	int				psupply_fan_on;
	int				cpu_fan_speed;
	int				system_fan_speed;
};

#define NUM_CHILDREN	8

struct bbc_i2c_bus {
	struct bbc_i2c_bus		*next;
	int				index;
	spinlock_t			lock;
	void				__iomem *i2c_bussel_reg;
	void				__iomem *i2c_control_regs;
	unsigned char			own, clock;

	wait_queue_head_t		wq;
	volatile int			waiting;

	struct list_head		temps;
	struct list_head		fans;

	struct platform_device		*op;
	struct {
		struct platform_device	*device;
		int			client_claimed;
	} devs[NUM_CHILDREN];
};

/* Probing and attachment. */
extern struct platform_device *bbc_i2c_getdev(struct bbc_i2c_bus *, int);
extern struct bbc_i2c_client *bbc_i2c_attach(struct bbc_i2c_bus *bp, struct platform_device *);
extern void bbc_i2c_detach(struct bbc_i2c_client *);

/* Register read/write.  NOTE: Blocking! */
extern int bbc_i2c_writeb(struct bbc_i2c_client *, unsigned char val, int off);
extern int bbc_i2c_readb(struct bbc_i2c_client *, unsigned char *byte, int off);
extern int bbc_i2c_write_buf(struct bbc_i2c_client *, char *buf, int len, int off);
extern int bbc_i2c_read_buf(struct bbc_i2c_client *, char *buf, int len, int off);

extern int bbc_envctrl_init(struct bbc_i2c_bus *bp);
extern void bbc_envctrl_cleanup(struct bbc_i2c_bus *bp);

#endif /* _BBC_I2C_H */
