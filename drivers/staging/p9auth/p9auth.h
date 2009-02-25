#ifndef CAP_MAJOR
#define CAP_MAJOR 0
#endif

#ifndef CAP_NR_DEVS
#define CAP_NR_DEVS 2		/* caphash and capuse */
#endif

#ifndef CAP_NODE_SIZE
#define CAP_NODE_SIZE 20
#endif

#define MAX_DIGEST_SIZE  20

struct cap_node {
	char data[CAP_NODE_SIZE];
	struct list_head list;
};

struct cap_dev {
	struct cap_node *head;
	int node_size;
	unsigned long size;
	struct semaphore sem;
	struct cdev cdev;
};

extern int cap_major;
extern int cap_nr_devs;
extern int cap_node_size;

int cap_trim(struct cap_dev *);
ssize_t cap_write(struct file *, const char __user *, size_t, loff_t *);
char *cap_hash(char *plain_text, unsigned int plain_text_size, char *key, unsigned int key_size);
void hex_dump(unsigned char * buf, unsigned int len);
