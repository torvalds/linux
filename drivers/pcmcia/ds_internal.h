/* ds_internal.h - internal header for 16-bit PCMCIA devices management */

struct user_info_t;

/* Socket state information */
struct pcmcia_bus_socket {
	struct kref		refcount;
	int			state;
	struct pcmcia_socket	*parent;

	/* the PCMCIA devices connected to this socket (normally one, more
	 * for multifunction devices: */
	struct list_head	devices_list;
	u8			device_count; /* the number of devices, used
					       * only internally and subject
					       * to incorrectness and change */

	u8			device_add_pending;
	struct work_struct	device_add;


#ifdef CONFIG_PCMCIA_IOCTL
	struct user_info_t	*user;
	wait_queue_head_t	queue;
#endif
};
extern spinlock_t pcmcia_dev_list_lock;

extern struct bus_type pcmcia_bus_type;


#define DS_SOCKET_PRESENT		0x01
#define DS_SOCKET_BUSY			0x02
#define DS_SOCKET_DEAD			0x80

extern struct pcmcia_device * pcmcia_get_dev(struct pcmcia_device *p_dev);
extern void pcmcia_put_dev(struct pcmcia_device *p_dev);

struct pcmcia_bus_socket *pcmcia_get_bus_socket(struct pcmcia_bus_socket *s);
void pcmcia_put_bus_socket(struct pcmcia_bus_socket *s);

struct pcmcia_device * pcmcia_device_add(struct pcmcia_bus_socket *s, unsigned int function);

#ifdef CONFIG_PCMCIA_IOCTL
extern void __init pcmcia_setup_ioctl(void);
extern void __exit pcmcia_cleanup_ioctl(void);
extern void handle_event(struct pcmcia_bus_socket *s, event_t event);
extern int handle_request(struct pcmcia_bus_socket *s, event_t event);
#else
static inline void __init pcmcia_setup_ioctl(void) { return; }
static inline void __init pcmcia_cleanup_ioctl(void) { return; }
static inline void handle_event(struct pcmcia_bus_socket *s, event_t event) { return; }
static inline int handle_request(struct pcmcia_bus_socket *s, event_t event) { return CS_SUCCESS; }
#endif
