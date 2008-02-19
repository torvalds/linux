#include <linux/pci.h>
#include <linux/init.h>
#include <linux/topology.h>

#define BUS_NR 256

static unsigned char mp_bus_to_node[BUS_NR];

void set_mp_bus_to_node(int busnum, int node)
{
	if (busnum >= 0 &&  busnum < BUS_NR)
	mp_bus_to_node[busnum] = (unsigned char) node;
}

int get_mp_bus_to_node(int busnum)
{
	int node;

	if (busnum < 0 || busnum > (BUS_NR - 1))
		return 0;
	node = mp_bus_to_node[busnum];
	return node;
}
