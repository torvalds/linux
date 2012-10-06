#ifndef __IIO_BFIN_TIMER_TRIGGER_H__
#define __IIO_BFIN_TIMER_TRIGGER_H__

/**
 * struct iio_bfin_timer_trigger_pdata - timer trigger platform data
 * @output_enable: Enable external trigger pulse generation.
 * @active_low: Whether the trigger pulse is active low.
 * @duty_ns: Length of the trigger pulse in nanoseconds.
 *
 * This struct is used to configure the output pulse generation of the blackfin
 * timer trigger. If output_enable is set to true an external trigger signal
 * will generated on the pin corresponding to the timer. This is useful for
 * converters which needs an external signal to start conversion. active_low and
 * duty_ns are used to configure the type of the trigger pulse. If output_enable
 * is set to false no external trigger pulse will be generated and active_low
 * and duty_ns are ignored.
 **/
struct iio_bfin_timer_trigger_pdata {
	bool output_enable;
	bool active_low;
	unsigned int duty_ns;
};

#endif
