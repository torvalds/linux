/*
 * Thunderbolt Cactus Ridge driver - PCIe tunnel
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 */

#ifndef TB_PCI_H_
#define TB_PCI_H_

#include "tb.h"

struct tb_pci_tunnel {
	struct tb *tb;
	struct tb_port *up_port;
	struct tb_port *down_port;
	struct tb_path *path_to_up;
	struct tb_path *path_to_down;
	struct list_head list;
};

struct tb_pci_tunnel *tb_pci_alloc(struct tb *tb, struct tb_port *up,
				   struct tb_port *down);
void tb_pci_free(struct tb_pci_tunnel *tunnel);
int tb_pci_activate(struct tb_pci_tunnel *tunnel);
int tb_pci_restart(struct tb_pci_tunnel *tunnel);
void tb_pci_deactivate(struct tb_pci_tunnel *tunnel);
bool tb_pci_is_invalid(struct tb_pci_tunnel *tunnel);

#endif

