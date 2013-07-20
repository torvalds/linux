#include <mach/gpio.h>
#include <mach/board.h>

int port_output_init(unsigned int value, int on, char *name)
{
        int ret = 0;
        struct port_config port;

        port = get_port_config(value);
        ret = gpio_request(port.gpio, name);
        if(ret < 0)
                return ret;
        if(port.io.pull_mode == PULL_MODE_DISABLE)
                gpio_pull_updown(port.gpio, 0);
        if(port.io.pull_mode == PULL_MODE_ENABLE)
                gpio_pull_updown(port.gpio, 1);
		#ifdef CONFIG_MACH_RK_FAC
        //gpio_direction_output(port.gpio, (on)? !port.io.active_low: !!port.io.active_low);
        #else
		  gpio_direction_output(port.gpio, (on)? !port.io.active_low: !!port.io.active_low);
		#endif 

        return 0;
}
EXPORT_SYMBOL(port_output_init);
void port_output_on(unsigned int value)
{
        struct port_config port;

        port = get_port_config(value);
        gpio_set_value(port.gpio, !port.io.active_low);
}
EXPORT_SYMBOL(port_output_on);
void port_output_off(unsigned int value)
{
        struct port_config port;

        port = get_port_config(value);
        gpio_set_value(port.gpio, !!port.io.active_low);
}
EXPORT_SYMBOL(port_output_off);
void port_deinit(unsigned int value)
{
        struct port_config port;

        port = get_port_config(value);
        gpio_free(port.gpio);
}
EXPORT_SYMBOL(port_deinit);
int port_input_init(unsigned int value, char *name)
{
        int ret = 0;
        struct port_config port;

        port = get_port_config(value);
        ret = gpio_request(port.gpio, name);
        if(ret < 0)
                return ret;
        if(port.io.pull_mode == PULL_MODE_DISABLE)
                gpio_pull_updown(port.gpio, 0);
        if(port.io.pull_mode == PULL_MODE_ENABLE)
                gpio_pull_updown(port.gpio, 1);
        gpio_direction_input(port.gpio);

        return 0;
}
EXPORT_SYMBOL(port_input_init);
int port_get_value(unsigned int value)
{
        struct port_config port;

        port = get_port_config(value);
        return gpio_get_value(port.gpio);
}
EXPORT_SYMBOL(port_get_value);


