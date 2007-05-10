#ifndef _PSMOUSE_H
#define _PSMOUSE_H

#define PSMOUSE_CMD_SETSCALE11	0x00e6
#define PSMOUSE_CMD_SETSCALE21	0x00e7
#define PSMOUSE_CMD_SETRES	0x10e8
#define PSMOUSE_CMD_GETINFO	0x03e9
#define PSMOUSE_CMD_SETSTREAM	0x00ea
#define PSMOUSE_CMD_SETPOLL	0x00f0
#define PSMOUSE_CMD_POLL	0x00eb	/* caller sets number of bytes to receive */
#define PSMOUSE_CMD_GETID	0x02f2
#define PSMOUSE_CMD_SETRATE	0x10f3
#define PSMOUSE_CMD_ENABLE	0x00f4
#define PSMOUSE_CMD_DISABLE	0x00f5
#define PSMOUSE_CMD_RESET_DIS	0x00f6
#define PSMOUSE_CMD_RESET_BAT	0x02ff

#define PSMOUSE_RET_BAT		0xaa
#define PSMOUSE_RET_ID		0x00
#define PSMOUSE_RET_ACK		0xfa
#define PSMOUSE_RET_NAK		0xfe

enum psmouse_state {
	PSMOUSE_IGNORE,
	PSMOUSE_INITIALIZING,
	PSMOUSE_RESYNCING,
	PSMOUSE_CMD_MODE,
	PSMOUSE_ACTIVATED,
};

/* psmouse protocol handler return codes */
typedef enum {
	PSMOUSE_BAD_DATA,
	PSMOUSE_GOOD_DATA,
	PSMOUSE_FULL_PACKET
} psmouse_ret_t;

struct psmouse {
	void *private;
	struct input_dev *dev;
	struct ps2dev ps2dev;
	struct work_struct resync_work;
	char *vendor;
	char *name;
	unsigned char packet[8];
	unsigned char badbyte;
	unsigned char pktcnt;
	unsigned char pktsize;
	unsigned char type;
	unsigned char acks_disable_command;
	unsigned int model;
	unsigned long last;
	unsigned long out_of_sync;
	unsigned long num_resyncs;
	enum psmouse_state state;
	char devname[64];
	char phys[32];

	unsigned int rate;
	unsigned int resolution;
	unsigned int resetafter;
	unsigned int resync_time;
	unsigned int smartscroll;	/* Logitech only */

	psmouse_ret_t (*protocol_handler)(struct psmouse *psmouse);
	void (*set_rate)(struct psmouse *psmouse, unsigned int rate);
	void (*set_resolution)(struct psmouse *psmouse, unsigned int resolution);

	int (*reconnect)(struct psmouse *psmouse);
	void (*disconnect)(struct psmouse *psmouse);
	void (*cleanup)(struct psmouse *psmouse);
	int (*poll)(struct psmouse *psmouse);

	void (*pt_activate)(struct psmouse *psmouse);
	void (*pt_deactivate)(struct psmouse *psmouse);
};

enum psmouse_type {
	PSMOUSE_NONE,
	PSMOUSE_PS2,
	PSMOUSE_PS2PP,
	PSMOUSE_THINKPS,
	PSMOUSE_GENPS,
	PSMOUSE_IMPS,
	PSMOUSE_IMEX,
	PSMOUSE_SYNAPTICS,
	PSMOUSE_ALPS,
	PSMOUSE_LIFEBOOK,
	PSMOUSE_TRACKPOINT,
	PSMOUSE_TOUCHKIT_PS2,
	PSMOUSE_CORTRON,
	PSMOUSE_AUTO		/* This one should always be last */
};

int psmouse_sliced_command(struct psmouse *psmouse, unsigned char command);
int psmouse_reset(struct psmouse *psmouse);
void psmouse_set_resolution(struct psmouse *psmouse, unsigned int resolution);


struct psmouse_attribute {
	struct device_attribute dattr;
	void *data;
	ssize_t (*show)(struct psmouse *psmouse, void *data, char *buf);
	ssize_t (*set)(struct psmouse *psmouse, void *data,
			const char *buf, size_t count);
};
#define to_psmouse_attr(a)	container_of((a), struct psmouse_attribute, dattr)

ssize_t psmouse_attr_show_helper(struct device *dev, struct device_attribute *attr,
				 char *buf);
ssize_t psmouse_attr_set_helper(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count);

#define PSMOUSE_DEFINE_ATTR(_name, _mode, _data, _show, _set)			\
static ssize_t _show(struct psmouse *, void *data, char *);			\
static ssize_t _set(struct psmouse *, void *data, const char *, size_t);	\
static struct psmouse_attribute psmouse_attr_##_name = {			\
	.dattr	= {								\
		.attr	= {							\
			.name	= __stringify(_name),				\
			.mode	= _mode,					\
			.owner	= THIS_MODULE,					\
		},								\
		.show	= psmouse_attr_show_helper,				\
		.store	= psmouse_attr_set_helper,				\
	},									\
	.data	= _data,							\
	.show	= _show,							\
	.set	= _set,								\
}

#endif /* _PSMOUSE_H */
