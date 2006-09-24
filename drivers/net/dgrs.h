/*
 *	ioctl's for the Digi Intl. RightSwitch
 *
 *	These network driver ioctl's are a bit obtuse compared to the usual
 *	ioctl's for a "normal" device driver.  Hey, I didn't invent it.
 *
 *	Typical use:
 *
 *	struct ifreq	ifr;
 *	DGRS_IOCTL	ioc;
 *	int		x;
 *
 *	strcpy(ifr.ifr_name, "eth1");
 *	ifr.ifr_data = (caddr_t) &ioc;
 *	ioc.cmd = DGRS_GETMEM;
 *	ioc.len = sizeof(x);
 *	ioc.data = (caddr_t) &x;
 *	rc = ioctl(fd, DGRSIOCTL, &ifr);
 *	printf("rc=%d mem=%x\n", rc, x);
 *
 */
#include <linux/sockios.h>

#define DGRSIOCTL      SIOCDEVPRIVATE

typedef struct dgrs_ioctl {
	unsigned short cmd;	/* Command to run */
	unsigned short len;	/* Length of the data buffer */
	unsigned char  __user *data;	/* Pointer to the data buffer */
	unsigned short	port;	/* port number for command, if needed */
	unsigned short	filter;	/* filter number for command, if needed */
} DGRS_IOCTL;

/*
 *	Commands for the driver
 */
#define	DGRS_GETMEM		0x01	/* Get the dual port memory address */
#define	DGRS_SETFILTER		0x02	/* Set a filter */
