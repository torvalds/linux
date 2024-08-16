/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Serial core related functions, serial port device drivers do not need this.
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tony Lindgren <tony@atomide.com>
 */

#define to_serial_base_ctrl_device(d) container_of((d), struct serial_ctrl_device, dev)
#define to_serial_base_port_device(d) container_of((d), struct serial_port_device, dev)

struct uart_driver;
struct uart_port;
struct device_driver;
struct device;

struct serial_ctrl_device {
	struct device dev;
	struct ida port_ida;
};

struct serial_port_device {
	struct device dev;
	struct uart_port *port;
	unsigned int tx_enabled:1;
};

int serial_base_ctrl_init(void);
void serial_base_ctrl_exit(void);

int serial_base_port_init(void);
void serial_base_port_exit(void);

void serial_base_port_startup(struct uart_port *port);
void serial_base_port_shutdown(struct uart_port *port);

int serial_base_driver_register(struct device_driver *driver);
void serial_base_driver_unregister(struct device_driver *driver);

struct serial_ctrl_device *serial_base_ctrl_add(struct uart_port *port,
						struct device *parent);
struct serial_port_device *serial_base_port_add(struct uart_port *port,
						struct serial_ctrl_device *parent);
void serial_base_ctrl_device_remove(struct serial_ctrl_device *ctrl_dev);
void serial_base_port_device_remove(struct serial_port_device *port_dev);

int serial_ctrl_register_port(struct uart_driver *drv, struct uart_port *port);
void serial_ctrl_unregister_port(struct uart_driver *drv, struct uart_port *port);

int serial_core_register_port(struct uart_driver *drv, struct uart_port *port);
void serial_core_unregister_port(struct uart_driver *drv, struct uart_port *port);

#ifdef CONFIG_SERIAL_CORE_CONSOLE

int serial_base_match_and_update_preferred_console(struct uart_driver *drv,
						   struct uart_port *port);

#else

static inline
int serial_base_match_and_update_preferred_console(struct uart_driver *drv,
						   struct uart_port *port)
{
	return 0;
}

#endif
