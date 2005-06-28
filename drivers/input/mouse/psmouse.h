#ifndef _PSMOUSE_H
#define _PSMOUSE_H

#define PSMOUSE_CMD_SETSCALE11	0x00e6
#define PSMOUSE_CMD_SETSCALE21	0x00e7
#define PSMOUSE_CMD_SETRES	0x10e8
#define PSMOUSE_CMD_GETINFO	0x03e9
#define PSMOUSE_CMD_SETSTREAM	0x00ea
#define PSMOUSE_CMD_SETPOLL	0x00f0
#define PSMOUSE_CMD_POLL	0x03eb
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
	struct input_dev dev;
	struct ps2dev ps2dev;
	char *vendor;
	char *name;
	unsigned char packet[8];
	unsigned char pktcnt;
	unsigned char pktsize;
	unsigned char type;
	unsigned int model;
	unsigned long last;
	unsigned long out_of_sync;
	enum psmouse_state state;
	char devname[64];
	char phys[32];

	unsigned int rate;
	unsigned int resolution;
	unsigned int resetafter;
	unsigned int smartscroll;	/* Logitech only */

	psmouse_ret_t (*protocol_handler)(struct psmouse *psmouse, struct pt_regs *regs);
	void (*set_rate)(struct psmouse *psmouse, unsigned int rate);
	void (*set_resolution)(struct psmouse *psmouse, unsigned int resolution);

	int (*reconnect)(struct psmouse *psmouse);
	void (*disconnect)(struct psmouse *psmouse);

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
	PSMOUSE_AUTO		/* This one should always be last */
};

int psmouse_sliced_command(struct psmouse *psmouse, unsigned char command);
int psmouse_reset(struct psmouse *psmouse);
void psmouse_set_resolution(struct psmouse *psmouse, unsigned int resolution);

ssize_t psmouse_attr_show_helper(struct device *dev, char *buf,
			ssize_t (*handler)(struct psmouse *, char *));
ssize_t psmouse_attr_set_helper(struct device *dev, const char *buf, size_t count,
			ssize_t (*handler)(struct psmouse *, const char *, size_t));

#define PSMOUSE_DEFINE_ATTR(_name)						\
static ssize_t psmouse_attr_show_##_name(struct psmouse *, char *);		\
static ssize_t psmouse_attr_set_##_name(struct psmouse *, const char *, size_t);\
static ssize_t psmouse_do_show_##_name(struct device *d, struct device_attribute *attr, char *b)		\
{										\
	return psmouse_attr_show_helper(d, b, psmouse_attr_show_##_name);	\
}										\
static ssize_t psmouse_do_set_##_name(struct device *d, struct device_attribute *attr, const char *b, size_t s)\
{										\
	return psmouse_attr_set_helper(d, b, s, psmouse_attr_set_##_name);	\
}										\
static struct device_attribute psmouse_attr_##_name =				\
	__ATTR(_name, S_IWUSR | S_IRUGO,					\
		psmouse_do_show_##_name, psmouse_do_set_##_name);

#endif /* _PSMOUSE_H */
