#define MAX_BUFFER_SIZE		144
#define MAX_FINGER_NUMBER	3
#define MAX_PRESSURE		15
#define DEVICE_NAME			"IT7260"
#define DEVICE_VENDOR		0
#define DEVICE_PRODUCT		0
#define DEVICE_VERSION		0
#define IT7260_X_RESOLUTION	1024
#define IT7260_Y_RESOLUTION	600
#define SCREEN_X_RESOLUTION	1024
#define SCREEN_Y_RESOLUTION	600
#define VERSION_ABOVE_ANDROID_20

//unsigned char bufferIndex;
//unsigned int length;
//unsigned char buffer[MAX_BUFFER_SIZE];
struct ioctl_cmd168 {
	unsigned short bufferIndex;
	unsigned short length;
	unsigned short buffer[MAX_BUFFER_SIZE];
};

#define IOC_MAGIC		'd'
#define IOCTL_SET 		_IOW(IOC_MAGIC, 1, struct ioctl_cmd168)
#define IOCTL_GET 		_IOR(IOC_MAGIC, 2, struct ioctl_cmd168)
#define IOCTL_READ_CDC 	_IOR(IOC_MAGIC, 0x10, struct ioctl_cmd168)
//#define IOCTL_SET 		_IOW(IOC_MAGIC, 1, struct ioctl_cmd)
//#define IOCTL_GET 		_IOR(IOC_MAGIC, 2, struct ioctl_cmd)
//#define IOCTL_READ_CDC 	_IOR(IOC_MAGIC, 0x10, struct ioctl_cmd)
