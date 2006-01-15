/* ds_internal.h - internal header for 16-bit PCMCIA devices management */

extern spinlock_t pcmcia_dev_list_lock;
extern struct bus_type pcmcia_bus_type;

extern struct pcmcia_device * pcmcia_get_dev(struct pcmcia_device *p_dev);
extern void pcmcia_put_dev(struct pcmcia_device *p_dev);

struct pcmcia_device * pcmcia_device_add(struct pcmcia_socket *s, unsigned int function);

#ifdef CONFIG_PCMCIA_IOCTL
extern void __init pcmcia_setup_ioctl(void);
extern void __exit pcmcia_cleanup_ioctl(void);
extern void handle_event(struct pcmcia_socket *s, event_t event);
extern int handle_request(struct pcmcia_socket *s, event_t event);
#else
static inline void __init pcmcia_setup_ioctl(void) { return; }
static inline void __exit pcmcia_cleanup_ioctl(void) { return; }
static inline void handle_event(struct pcmcia_socket *s, event_t event) { return; }
static inline int handle_request(struct pcmcia_socket *s, event_t event) { return CS_SUCCESS; }
#endif
