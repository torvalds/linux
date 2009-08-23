#ifndef __RSPIUSB_H
#define __RSPIUSB_H

#define PIUSB_MAGIC		'm'
#define PIUSB_IOCTL_BASE	192

#define PIUSB_IOR(offset) \
	_IOR(PIUSB_MAGIC, PIUSB_IOCTL_BASE + offset, struct ioctl_struct)
#define PIUSB_IOW(offset) \
	_IOW(PIUSB_MAGIC, PIUSB_IOCTL_BASE + offset, struct ioctl_struct)
#define PIUSB_IO(offset) \
	_IO(PIUSB_MAGIC, PIUSB_IOCTL_BASE + offset)

#define PIUSB_GETVNDCMD		PIUSB_IOR(1)
#define PIUSB_SETVNDCMD		PIUSB_IOW(2)
#define PIUSB_WRITEPIPE		PIUSB_IOW(3)
#define PIUSB_READPIPE		PIUSB_IOR(4)
#define PIUSB_SETFRAMESIZE	PIUSB_IOW(5)
#define PIUSB_WHATCAMERA	PIUSB_IO(6)
#define PIUSB_USERBUFFER	PIUSB_IOW(7)
#define PIUSB_ISHIGHSPEED	PIUSB_IO(8)
#define PIUSB_UNMAP_USERBUFFER	PIUSB_IOW(9)

struct ioctl_struct {
	unsigned char cmd;
	unsigned long numbytes;
	unsigned char dir;	/* 1=out; 0=in */
	int endpoint;
	int numFrames;
	unsigned char *pData;
};

#endif
