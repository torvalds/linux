/* 2MB large area for motherboard's peripherals static mapping */
#define V2M_PERIPH 0xf8000000

/* Tile's peripherals static mappings should start here */
#define V2T_PERIPH 0xf8200000

void vexpress_dt_smp_map_io(void);

bool vexpress_smp_init_ops(void);

extern struct smp_operations	vexpress_smp_ops;

extern void vexpress_cpu_die(unsigned int cpu);
