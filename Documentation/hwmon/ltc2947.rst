Kernel drivers ltc2947-i2c and ltc2947-spi
==========================================

Supported chips:

  * Analog Devices LTC2947

    Prefix: 'ltc2947'

    Addresses scanned: -

    Datasheet:

        https://www.analog.com/media/en/technical-documentation/data-sheets/LTC2947.pdf

Author: Nuno Sá <nuno.sa@analog.com>

Description
___________

The LTC2947 is a high precision power and energy monitor that measures current,
voltage, power, temperature, charge and energy. The device supports both SPI
and I2C depending on the chip configuration.
The device also measures accumulated quantities as energy. It has two banks of
register's to read/set energy related values. These banks can be configured
independently to have setups like: energy1 accumulates always and enrgy2 only
accumulates if current is positive (to check battery charging efficiency for
example). The device also supports a GPIO pin that can be configured as output
to control a fan as a function of measured temperature. Then, the GPIO becomes
active as soon as a temperature reading is higher than a defined threshold. The
temp2 channel is used to control this thresholds and to read the respective
alarms.

Sysfs entries
_____________

The following attributes are supported. Limits are read-write, reset_history
is write-only and all the other attributes are read-only.

======================= ==========================================
in0_input		VP-VM voltage (mV).
in0_min			Undervoltage threshold
in0_max			Overvoltage threshold
in0_lowest		Lowest measured voltage
in0_highest		Highest measured voltage
in0_reset_history	Write 1 to reset in1 history
in0_min_alarm		Undervoltage alarm
in0_max_alarm		Overvoltage alarm
in0_label		Channel label (VP-VM)

in1_input		DVCC voltage (mV)
in1_min			Undervoltage threshold
in1_max			Overvoltage threshold
in1_lowest		Lowest measured voltage
in1_highest		Highest measured voltage
in1_reset_history	Write 1 to reset in2 history
in1_min_alarm		Undervoltage alarm
in1_max_alarm		Overvoltage alarm
in1_label		Channel label (DVCC)

curr1_input		IP-IM Sense current (mA)
curr1_min		Undercurrent threshold
curr1_max		Overcurrent threshold
curr1_lowest		Lowest measured current
curr1_highest		Highest measured current
curr1_reset_history	Write 1 to reset curr1 history
curr1_min_alarm		Undercurrent alarm
curr1_max_alarm		Overcurrent alarm
curr1_label		Channel label (IP-IM)

power1_input		Power (in uW)
power1_min		Low power threshold
power1_max		High power threshold
power1_input_lowest	Historical minimum power use
power1_input_highest	Historical maximum power use
power1_reset_history	Write 1 to reset power1 history
power1_min_alarm	Low power alarm
power1_max_alarm	High power alarm
power1_label		Channel label (Power)

temp1_input		Chip Temperature (in milliC)
temp1_min		Low temperature threshold
temp1_max		High temperature threshold
temp1_input_lowest	Historical minimum temperature use
temp1_input_highest	Historical maximum temperature use
temp1_reset_history	Write 1 to reset temp1 history
temp1_min_alarm		Low temperature alarm
temp1_max_alarm		High temperature alarm
temp1_label		Channel label (Ambient)

temp2_min		Low temperature threshold for fan control
temp2_max		High temperature threshold for fan control
temp2_min_alarm		Low temperature fan control alarm
temp2_max_alarm		High temperature fan control alarm
temp2_label		Channel label (TEMPFAN)

energy1_input		Measured energy over time (in microJoule)

energy2_input		Measured energy over time (in microJoule)
======================= ==========================================
