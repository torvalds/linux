#ifndef __ASM_AVR32_ATMEL_MCI_H
#define __ASM_AVR32_ATMEL_MCI_H

/**
 * struct mci_slot_pdata - board-specific per-slot configuration
 * @bus_width: Number of data lines wired up the slot
 * @detect_pin: GPIO pin wired to the card detect switch
 * @wp_pin: GPIO pin wired to the write protect sensor
 *
 * If a given slot is not present on the board, @bus_width should be
 * set to 0. The other fields are ignored in this case.
 *
 * Any pins that aren't available should be set to a negative value.
 */
struct mci_slot_pdata {
	unsigned int		bus_width;
	int			detect_pin;
	int			wp_pin;
};

/**
 * struct mci_platform_data - board-specific MMC/SDcard configuration
 * @slot: Per-slot configuration data.
 */
struct mci_platform_data {
	struct mci_slot_pdata	slot[2];
};

#endif /* __ASM_AVR32_ATMEL_MCI_H */
