#ifndef __REBOOT_MODE_H__
#define __REBOOT_MODE_H__

struct reboot_mode_driver {
	struct device *dev;
	struct list_head head;
	int (*write)(struct reboot_mode_driver *reboot, unsigned int magic);
	struct notifier_block reboot_notifier;
};

int reboot_mode_register(struct reboot_mode_driver *reboot);
int reboot_mode_unregister(struct reboot_mode_driver *reboot);

#endif
