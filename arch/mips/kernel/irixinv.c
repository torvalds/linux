/*
 * Support the inventory interface for IRIX binaries
 * This is invoked before the mm layer is working, so we do not
 * use the linked lists for the inventory yet.
 *
 * Miguel de Icaza, 1997.
 */
#include <linux/mm.h>
#include <asm/inventory.h>
#include <asm/uaccess.h>

#define MAX_INVENTORY 50
int inventory_items = 0;

static inventory_t inventory [MAX_INVENTORY];

void add_to_inventory (int class, int type, int controller, int unit, int state)
{
	inventory_t *ni = &inventory [inventory_items];

	if (inventory_items == MAX_INVENTORY)
		return;

	ni->inv_class      = class;
	ni->inv_type       = type;
	ni->inv_controller = controller;
	ni->inv_unit       = unit;
	ni->inv_state      = state;
	ni->inv_next       = ni;
	inventory_items++;
}

int dump_inventory_to_user (void *userbuf, int size)
{
	inventory_t *inv  = &inventory [0];
	inventory_t *user = userbuf;
	int v;

	if (!access_ok(VERIFY_WRITE, userbuf, size))
		return -EFAULT;

	for (v = 0; v < inventory_items; v++){
		inv = &inventory [v];
		copy_to_user (user, inv, sizeof (inventory_t));
		user++;
	}
	return inventory_items * sizeof (inventory_t);
}

int __init init_inventory(void)
{
	/*
	 * gross hack while we put the right bits all over the kernel
	 * most likely this will not let just anyone run the X server
	 * until we put the right values all over the place
	 */
	add_to_inventory (10, 3, 0, 0, 16400);
	add_to_inventory (1, 1, 150, -1, 12);
	add_to_inventory (1, 3, 0, 0, 8976);
	add_to_inventory (1, 2, 0, 0, 8976);
	add_to_inventory (4, 8, 0, 0, 2);
	add_to_inventory (5, 5, 0, 0, 1);
	add_to_inventory (3, 3, 0, 0, 32768);
	add_to_inventory (3, 4, 0, 0, 32768);
	add_to_inventory (3, 8, 0, 0, 524288);
	add_to_inventory (3, 9, 0, 0, 64);
	add_to_inventory (3, 1, 0, 0, 67108864);
	add_to_inventory (12, 3, 0, 0, 16);
	add_to_inventory (8, 7, 17, 0, 16777472);
	add_to_inventory (8, 0, 0, 0, 1);
	add_to_inventory (2, 1, 0, 13, 2);
	add_to_inventory (2, 2, 0, 2, 0);
	add_to_inventory (2, 2, 0, 1, 0);
	add_to_inventory (7, 14, 0, 0, 6);

	return 0;
}
