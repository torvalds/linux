#ifndef _PSMOUSE_H
#define _PSMOUSE_H

#define PSMOUSE_CMD_SETSCALE11	0x00e6
#define PSMOUSE_CMD_SETSCALE21	0x00e7
#define PSMOUSE_CMD_SETRES	0x10e8
#define PSMOUSE_CMD_GETINFO	0x03e9
#define PSMOUSE_CMD_SETSTREAM	0x00ea
#define PSMOUSE_CMD_SETPOLL	0x00f0
#define PSMOUSE_CMD_POLL	0x00eb	/* caller sets number of bytes to receive */
#define PSMOUSE_CMD_RESET_WRAP	0x00ec
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

enum psmouse_scale {
	PSMOUSE_SCALE11,
	PSMOUSE_SCALE21
};

struct psmouse {
	void *private;
	struct input_dev *dev;
	struct ps2dev ps2dev;
	struct delayed_work resync_work;
	char *vendor;
	char *name;
	unsigned char packet[8];
	unsigned char badbyte;
	unsigned char pktcnt;
	unsigned char pktsize;
	unsigned char type;
	bool ignore_parity;
	bool acks_disable_command;
	unsigned int model;
	unsigned long last;
	unsigned long out_of_sync_cnt;
	unsigned long num_resyncs;
	enum psmouse_state state;
	char devname[64];
	char phys[32];

	unsigned int rate;
	unsigned int resolution;
	unsigned int resetafter;
	unsigned int resync_time;
	bool smartscroll;	/* Logitech only */

	psmouse_ret_t (*protocol_handler)(struct psmouse *psmouse);
	void (*set_rate)(struct psmouse *psmouse, unsigned int rate);
	void (*set_resolution)(struct psmouse *psmouse, unsigned int resolution);
	void (*set_scale)(struct psmouse *psmouse, enum psmouse_scale scale);

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
	PSMOUSE_HGPK,
	PSMOUSE_ELANTECH,
	PSMOUSE_FSP,
	PSMOUSE_SYNAPTICS_RELATIVE,
	PSMOUSE_CYPRESS,
	PSMOUSE_FOCALTECH,
	PSMOUSE_AUTO		/* This one should always be last */
};

void psmouse_queue_work(struct psmouse *psmouse, struct delayed_work *work,
		unsigned long delay);
int psmouse_sliced_command(struct psmouse *psmouse, unsigned char command);
int psmouse_reset(struct psmouse *psmouse);
void psmouse_set_state(struct psmouse *psmouse, enum psmouse_state new_state);
void psmouse_set_resolution(struct psmouse *psmouse, unsigned int resolution);
psmouse_ret_t psmouse_process_byte(struct psmouse *psmouse);
int psmouse_activate(struct psmouse *psmouse);
int psmouse_deactivate(struct psmouse *psmouse);
bool psmouse_matches_pnp_id(struct psmouse *psmouse, const char * const ids[]);

struct psmouse_attribute {
	struct device_attribute dattr;
	void *data;
	ssize_t (*show)(struct psmouse *psmouse, void *data, char *buf);
	ssize_t (*set)(struct psmouse *psmouse, void *data,
			const char *buf, size_t count);
	bool protect;
};
#define to_psmouse_attr(a)	container_of((a), struct psmouse_attribute, dattr)

ssize_t psmouse_attr_show_helper(struct device *dev, struct device_attribute *attr,
				 char *buf);
ssize_t psmouse_attr_set_helper(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count);

#define __PSMOUSE_DEFINE_ATTR_VAR(_name, _mode, _data, _show, _set, _protect)	\
static struct psmouse_attribute psmouse_attr_##_name = {			\
	.dattr	= {								\
		.attr	= {							\
			.name	= __stringify(_name),				\
			.mode	= _mode,					\
		},								\
		.show	= psmouse_attr_show_helper,				\
		.store	= psmouse_attr_set_helper,				\
	},									\
	.data	= _data,							\
	.show	= _show,							\
	.set	= _set,								\
	.protect = _protect,							\
}

#define __PSMOUSE_DEFINE_ATTR(_name, _mode, _data, _show, _set, _protect)	\
	static ssize_t _show(struct psmouse *, void *, char *);			\
	static ssize_t _set(struct psmouse *, void *, const char *, size_t);	\
	__PSMOUSE_DEFINE_ATTR_VAR(_name, _mode, _data, _show, _set, _protect)

#define PSMOUSE_DEFINE_ATTR(_name, _mode, _data, _show, _set)			\
	__PSMOUSE_DEFINE_ATTR(_name, _mode, _data, _show, _set, true)

#define PSMOUSE_DEFINE_RO_ATTR(_name, _mode, _data, _show)			\
	static ssize_t _show(struct psmouse *, void *, char *);			\
	__PSMOUSE_DEFINE_ATTR_VAR(_name, _mode, _data, _show, NULL, true)

#define PSMOUSE_DEFINE_WO_ATTR(_name, _mode, _data, _set)			\
	static ssize_t _set(struct psmouse *, void *, const char *, size_t);	\
	__PSMOUSE_DEFINE_ATTR_VAR(_name, _mode, _data, NULL, _set, true)

#ifndef psmouse_fmt
#define psmouse_fmt(fmt)	KBUILD_BASENAME ": " fmt
#endif

#define psmouse_dbg(psmouse, format, ...)		\
	dev_dbg(&(psmouse)->ps2dev.serio->dev,		\
		psmouse_fmt(format), ##__VA_ARGS__)
#define psmouse_info(psmouse, format, ...)		\
	dev_info(&(psmouse)->ps2dev.serio->dev,		\
		 psmouse_fmt(format), ##__VA_ARGS__)
#define psmouse_warn(psmouse, format, ...)		\
	dev_warn(&(psmouse)->ps2dev.serio->dev,		\
		 psmouse_fmt(format), ##__VA_ARGS__)
#define psmouse_err(psmouse, format, ...)		\
	dev_err(&(psmouse)->ps2dev.serio->dev,		\
		psmouse_fmt(format), ##__VA_ARGS__)
#define psmouse_notice(psmouse, format, ...)		\
	dev_notice(&(psmouse)->ps2dev.serio->dev,	\
		   psmouse_fmt(format), ##__VA_ARGS__)
#define psmouse_printk(level, psmouse, format, ...)	\
	dev_printk(level,				\
		   &(psmouse)->ps2dev.serio->dev,	\
		   psmouse_fmt(format), ##__VA_ARGS__)


#endif /* _PSMOUSE_H */
