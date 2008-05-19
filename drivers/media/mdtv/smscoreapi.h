#ifndef __smscoreapi_h__
#define __smscoreapi_h__

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define SMS_ALLOC_ALIGNMENT					128
#define SMS_DMA_ALIGNMENT					16
#define SMS_ALIGN_ADDRESS(addr) ((((u32)(addr)) + (SMS_DMA_ALIGNMENT-1)) & ~(SMS_DMA_ALIGNMENT-1))

#define SMS_DEVICE_FAMILY2					1
#define SMS_ROM_NO_RESPONSE					2
#define SMS_DEVICE_NOT_READY				0x8000000

typedef struct _smscore_device smscore_device_t;
typedef struct _smscore_client smscore_client_t;
typedef struct _smscore_buffer smscore_buffer_t;

typedef int (*hotplug_t)(smscore_device_t *coredev, struct device *device, int arrival);

typedef int (*setmode_t)(void *context, int mode);
typedef void (*detectmode_t)(void *context, int *mode);
typedef int (*sendrequest_t)(void *context, void *buffer, size_t size);
typedef int (*loadfirmware_t)(void *context, void *buffer, size_t size);
typedef int (*preload_t)(void *context);
typedef int (*postload_t)(void *context);

typedef int (*onresponse_t)(void *context, smscore_buffer_t *cb);
typedef void (*onremove_t)(void *context);

typedef struct _smscore_buffer
{
	// public members, once passed to clients can be changed freely
	struct list_head entry;
	int				size;
	int				offset;

	// private members, read-only for clients
	void			*p;
	dma_addr_t		phys;
	unsigned long	offset_in_common;
} *psmscore_buffer_t;

typedef struct _smsdevice_params
{
	struct device	*device;

	int				buffer_size;
	int				num_buffers;

	char			devpath[32];
	unsigned long	flags;

	setmode_t		setmode_handler;
	detectmode_t	detectmode_handler;
	sendrequest_t	sendrequest_handler;
	preload_t		preload_handler;
	postload_t		postload_handler;

	void			*context;
} smsdevice_params_t;

typedef struct _smsclient_params
{
	int				initial_id;
	int				data_type;
	onresponse_t	onresponse_handler;
	onremove_t		onremove_handler;

	void			*context;
} smsclient_params_t;

extern void smscore_registry_setmode(char *devpath, int mode);
extern int smscore_registry_getmode(char *devpath);

extern int smscore_register_hotplug(hotplug_t hotplug);
extern void smscore_unregister_hotplug(hotplug_t hotplug);

extern int smscore_register_device(smsdevice_params_t *params, smscore_device_t **coredev);
extern void smscore_unregister_device(smscore_device_t *coredev);

extern int smscore_start_device(smscore_device_t *coredev);
extern int smscore_load_firmware(smscore_device_t *coredev, char* filename, loadfirmware_t loadfirmware_handler);

extern int smscore_set_device_mode(smscore_device_t *coredev, int mode);
extern int smscore_get_device_mode(smscore_device_t *coredev);

extern int smscore_register_client(smscore_device_t *coredev, smsclient_params_t* params, smscore_client_t **client);
extern void smscore_unregister_client(smscore_client_t *client);

extern int smsclient_sendrequest(smscore_client_t *client, void *buffer, size_t size);
extern void smscore_onresponse(smscore_device_t *coredev, smscore_buffer_t *cb);

extern int smscore_get_common_buffer_size(smscore_device_t *coredev);
extern int smscore_map_common_buffer(smscore_device_t *coredev, struct vm_area_struct * vma);

extern smscore_buffer_t *smscore_getbuffer(smscore_device_t *coredev);
extern void smscore_putbuffer(smscore_device_t *coredev, smscore_buffer_t *cb);

#endif // __smscoreapi_h__
