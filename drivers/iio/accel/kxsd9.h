#include <linux/device.h>
#include <linux/kernel.h>

#define KXSD9_STATE_RX_SIZE 2
#define KXSD9_STATE_TX_SIZE 2

struct kxsd9_transport;

/**
 * struct kxsd9_transport - transport adapter for SPI or I2C
 * @trdev: transport device such as SPI or I2C
 * @readreg(): function to read a byte from an address in the device
 * @writereg(): function to write a byte to an address in the device
 * @readval(): function to read a 16bit value from the device
 * @rx: cache aligned read buffer
 * @tx: cache aligned write buffer
 */
struct kxsd9_transport {
	void *trdev;
	int (*readreg) (struct kxsd9_transport *tr, u8 address);
	int (*writereg) (struct kxsd9_transport *tr, u8 address, u8 val);
	int (*readval) (struct kxsd9_transport *tr, u8 address);
	u8 rx[KXSD9_STATE_RX_SIZE] ____cacheline_aligned;
	u8 tx[KXSD9_STATE_TX_SIZE];
};

int kxsd9_common_probe(struct device *parent,
		       struct kxsd9_transport *transport,
		       const char *name);
int kxsd9_common_remove(struct device *parent);
