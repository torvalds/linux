#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

struct sys_timer;

extern void timer_init(int irq);

extern struct sys_timer pxa168_timer;
extern struct sys_timer pxa910_timer;
extern void __init pxa168_init_irq(void);
extern void __init pxa910_init_irq(void);

extern void __init icu_init_irq(void);
extern void __init pxa_map_io(void);
