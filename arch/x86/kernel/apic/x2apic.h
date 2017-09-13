/* Common bits for X2APIC cluster/physical modes. */

int x2apic_apic_id_valid(int apicid);
int x2apic_apic_id_registered(void);
void __x2apic_send_IPI_dest(unsigned int apicid, int vector, unsigned int dest);
unsigned int x2apic_get_apic_id(unsigned long id);
unsigned long x2apic_set_apic_id(unsigned int id);
int x2apic_phys_pkg_id(int initial_apicid, int index_msb);
void x2apic_send_IPI_self(int vector);
