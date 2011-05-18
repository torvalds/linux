#ifndef B43_BUS_H_
#define B43_BUS_H_

enum b43_bus_type {
	B43_BUS_SSB,
};

struct b43_bus_dev {
	enum b43_bus_type bus_type;
	union {
		struct ssb_device *sdev;
	};

	u16 (*read16)(struct b43_bus_dev *dev, u16 offset);
	u32 (*read32)(struct b43_bus_dev *dev, u16 offset);
	void (*write16)(struct b43_bus_dev *dev, u16 offset, u16 value);
	void (*write32)(struct b43_bus_dev *dev, u16 offset, u32 value);
	void (*block_read)(struct b43_bus_dev *dev, void *buffer,
			   size_t count, u16 offset, u8 reg_width);
	void (*block_write)(struct b43_bus_dev *dev, const void *buffer,
			    size_t count, u16 offset, u8 reg_width);
};

struct b43_bus_dev *b43_bus_dev_ssb_init(struct ssb_device *sdev);

#endif /* B43_BUS_H_ */
