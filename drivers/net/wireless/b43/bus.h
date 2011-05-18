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
};

struct b43_bus_dev *b43_bus_dev_ssb_init(struct ssb_device *sdev);

#endif /* B43_BUS_H_ */
