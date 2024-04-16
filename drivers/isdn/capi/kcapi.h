/*
 * Kernel CAPI 2.0 Module
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * Copyright 2002 by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */


#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/isdn/capilli.h>

#ifdef KCAPI_DEBUG
#define DBG(format, arg...) do {					\
		printk(KERN_DEBUG "%s: " format "\n" , __func__ , ## arg); \
	} while (0)
#else
#define DBG(format, arg...) /* */
#endif

enum {
	CAPI_CTR_DETACHED = 0,
	CAPI_CTR_DETECTED = 1,
	CAPI_CTR_LOADING  = 2,
	CAPI_CTR_RUNNING  = 3,
};

extern struct capi_ctr *capi_controller[CAPI_MAXCONTR];
extern struct mutex capi_controller_lock;

extern struct capi20_appl *capi_applications[CAPI_MAXAPPL];

void kcapi_proc_init(void);
void kcapi_proc_exit(void);

struct capi20_appl {
	u16 applid;
	capi_register_params rparam;
	void (*recv_message)(struct capi20_appl *ap, struct sk_buff *skb);
	void *private;

	/* internal to kernelcapi.o */
	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;
	struct mutex recv_mtx;
	struct sk_buff_head recv_queue;
	struct work_struct recv_work;
	int release_in_progress;
};

u16 capi20_isinstalled(void);
u16 capi20_register(struct capi20_appl *ap);
u16 capi20_release(struct capi20_appl *ap);
u16 capi20_put_message(struct capi20_appl *ap, struct sk_buff *skb);
u16 capi20_get_manufacturer(u32 contr, u8 buf[CAPI_MANUFACTURER_LEN]);
u16 capi20_get_version(u32 contr, struct capi_version *verp);
u16 capi20_get_serial(u32 contr, u8 serial[CAPI_SERIAL_LEN]);
u16 capi20_get_profile(u32 contr, struct capi_profile *profp);
int capi20_manufacturer(unsigned long cmd, void __user *data);

#define CAPICTR_UP			0
#define CAPICTR_DOWN			1

int kcapi_init(void);
void kcapi_exit(void);

/*----- basic-type definitions -----*/

typedef __u8 *_cstruct;

typedef enum {
	CAPI_COMPOSE,
	CAPI_DEFAULT
} _cmstruct;

/*
   The _cmsg structure contains all possible CAPI 2.0 parameter.
   All parameters are stored here first. The function CAPI_CMSG_2_MESSAGE
   assembles the parameter and builds CAPI2.0 conform messages.
   CAPI_MESSAGE_2_CMSG disassembles CAPI 2.0 messages and stores the
   parameter in the _cmsg structure
 */

typedef struct {
	/* Header */
	__u16 ApplId;
	__u8 Command;
	__u8 Subcommand;
	__u16 Messagenumber;

	/* Parameter */
	union {
		__u32 adrController;
		__u32 adrPLCI;
		__u32 adrNCCI;
	} adr;

	_cmstruct AdditionalInfo;
	_cstruct B1configuration;
	__u16 B1protocol;
	_cstruct B2configuration;
	__u16 B2protocol;
	_cstruct B3configuration;
	__u16 B3protocol;
	_cstruct BC;
	_cstruct BChannelinformation;
	_cmstruct BProtocol;
	_cstruct CalledPartyNumber;
	_cstruct CalledPartySubaddress;
	_cstruct CallingPartyNumber;
	_cstruct CallingPartySubaddress;
	__u32 CIPmask;
	__u32 CIPmask2;
	__u16 CIPValue;
	__u32 Class;
	_cstruct ConnectedNumber;
	_cstruct ConnectedSubaddress;
	__u32 Data;
	__u16 DataHandle;
	__u16 DataLength;
	_cstruct FacilityConfirmationParameter;
	_cstruct Facilitydataarray;
	_cstruct FacilityIndicationParameter;
	_cstruct FacilityRequestParameter;
	__u16 FacilitySelector;
	__u16 Flags;
	__u32 Function;
	_cstruct HLC;
	__u16 Info;
	_cstruct InfoElement;
	__u32 InfoMask;
	__u16 InfoNumber;
	_cstruct Keypadfacility;
	_cstruct LLC;
	_cstruct ManuData;
	__u32 ManuID;
	_cstruct NCPI;
	__u16 Reason;
	__u16 Reason_B3;
	__u16 Reject;
	_cstruct Useruserdata;

	/* intern */
	unsigned l, p;
	unsigned char *par;
	__u8 *m;

	/* buffer to construct message */
	__u8 buf[180];

} _cmsg;

/*-----------------------------------------------------------------------*/

/*
 * Debugging / Tracing functions
 */

char *capi_cmd2str(__u8 cmd, __u8 subcmd);

typedef struct {
	u_char	*buf;
	u_char	*p;
	size_t	size;
	size_t	pos;
} _cdebbuf;

#define	CDEBUG_SIZE	1024
#define	CDEBUG_GSIZE	4096

void cdebbuf_free(_cdebbuf *cdb);
int cdebug_init(void);
void cdebug_exit(void);

_cdebbuf *capi_message2str(__u8 *msg);
