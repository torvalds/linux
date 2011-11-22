#ifndef _CMMB_CLASS_H_
#define _CMMB_CLASS_H_


#include <linux/types.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/list.h>

#define CMMB_MAJOR 200


#define CMMB_DEVICE_TUNER   0
#define CMMB_DEVICE_DEMO    1
#define CMMB_DEVICE_DEMUX   2
#define CMMB_DEVICE_CA      3
#define CMMB_DEVICE_MEMO    4

extern struct class * cmmb_class;

struct cmmb_adapter {
	int num;
	struct list_head list_head;
	struct list_head device_list;
	const char *name;
	void* priv;
	struct device *device;
};


extern struct cmmb_adapter CMMB_adapter;
struct cmmb_device {
	struct list_head list_head;
	struct file_operations *fops;
	struct cmmb_adapter *adapter;
	int type;
	u32 id;

	wait_queue_head_t	  wait_queue;

	int (*kernel_ioctl)(struct inode *inode, struct file *file,
			    unsigned int cmd, void *arg);

	void *priv;
};


int cmmb_register_device(struct cmmb_adapter *adap, struct cmmb_device **pcmmbdev,
			 struct file_operations *fops, void *priv, int type,char* name);
void cmmb_unregister_device(struct cmmb_device *cmmbdev);

#define cmmb_attach(FUNCTION, ARGS...) ({ \
	FUNCTION(ARGS); \


#endif/* #ifndef _CMMB_CLASS_H_ */
