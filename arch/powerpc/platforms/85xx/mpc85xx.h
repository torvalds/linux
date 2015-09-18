#ifndef MPC85xx_H
#define MPC85xx_H
extern int mpc85xx_common_publish_devices(void);

#ifdef CONFIG_CPM2
extern void mpc85xx_cpm2_pic_init(void);
#else
static inline void __init mpc85xx_cpm2_pic_init(void) {}
#endif /* CONFIG_CPM2 */

#ifdef CONFIG_QUICC_ENGINE
extern void mpc85xx_qe_init(void);
extern void mpc85xx_qe_par_io_init(void);
#else
static inline void __init mpc85xx_qe_init(void) {}
static inline void __init mpc85xx_qe_par_io_init(void) {}
#endif

#endif
