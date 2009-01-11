#ifndef __RSPIUSB_H
#define __RSPIUSB_H

#define PIUSB_MAGIC		'm'
#define PIUSB_IOCTL_BASE	192
#define PIUSB_GETVNDCMD		_IOR(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 1, struct ioctl_struct)
#define PIUSB_SETVNDCMD		_IOW(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 2, struct ioctl_struct)
#define PIUSB_WRITEPIPE		_IOW(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 3, struct ioctl_struct)
#define PIUSB_READPIPE		_IOR(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 4, struct ioctl_struct)
#define PIUSB_SETFRAMESIZE	_IOW(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 5, struct ioctl_struct)
#define PIUSB_WHATCAMERA	_IO(PIUSB_MAGIC,  PIUSB_IOCTL_BASE + 6)
#define PIUSB_USERBUFFER	_IOW(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 7, struct ioctl_struct)
#define PIUSB_ISHIGHSPEED	_IO(PIUSB_MAGIC,  PIUSB_IOCTL_BASE + 8)
#define PIUSB_UNMAP_USERBUFFER	_IOW(PIUSB_MAGIC, PIUSB_IOCTL_BASE + 9, struct ioctl_struct)

struct ioctl_struct {
	unsigned char cmd;
	unsigned long numbytes;
	unsigned char dir;	//1=out;0=in
	int endpoint;
	int numFrames;
	unsigned char *pData;
};

#endif
