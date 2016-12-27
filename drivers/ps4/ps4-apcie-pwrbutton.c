#include <linux/input.h>
#include "aeolia.h"

void icc_pwrbutton_trigger(struct apcie_dev *sc, int state)
{
	if (sc->icc.pwrbutton_dev) {
		input_report_key(sc->icc.pwrbutton_dev, KEY_POWER, state ? 1 : 0);
		input_sync(sc->icc.pwrbutton_dev);
	}
}

int icc_pwrbutton_init(struct apcie_dev *sc)
{
	int ret = 0;
	u16 button;
	struct input_dev *dev;

	dev = input_allocate_device();
	if (!dev) {
		sc_err("%s: Not enough memory.\n", __func__);
		return -ENOMEM;
	}

	set_bit(EV_KEY, dev->evbit);
	set_bit(KEY_POWER, dev->keybit);

	dev->name = "Power Button";
	dev->id.bustype = BUS_HOST;

	/* this makes the button look like an acpi power button
	 * no clue whether anyone relies on that though */
	dev->id.product = 0x02;
	dev->phys = "LNXPWRBN/button/input0";

	dev->dev.parent = &sc->pdev->dev;
	ret = input_register_device(dev);
	if (ret) {
		sc_err("%s: Failed to register device\n", __func__);
		input_free_device(dev);
		return ret;
	}

	sc->icc.pwrbutton_dev = dev;

	// enable power button notifications
	button = 0x100;
	ret = apcie_icc_cmd(8, 1, &button, sizeof(button), NULL, 0);
	if (ret < 0) {
		sc_info("%s: Failed to enable power notifications (%d)\n",
			__func__, ret);
	}

	// enable reset button notifications (?)
	button = 0x102;
	ret = apcie_icc_cmd(8, 1, &button, sizeof(button), NULL, 0);
	if (ret < 0) {
		sc_info("%s: Failed to enable reset notifications (%d)\n",
		        __func__, ret);
	}

	return 0;
}

void icc_pwrbutton_remove(struct apcie_dev *sc)
{
	if (sc->icc.pwrbutton_dev)
		input_free_device(sc->icc.pwrbutton_dev);
	sc->icc.pwrbutton_dev = NULL;
}
