/*
 * Definitions for API from sdio common code (bcmsdh) to individual
 * host controller drivers.
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: bcmsdbus.h 408155 2013-06-17 21:52:27Z $
 */

#ifndef	_sdio_api_h_
#define	_sdio_api_h_


#define SDIOH_API_RC_SUCCESS                          (0x00)
#define SDIOH_API_RC_FAIL	                      (0x01)
#define SDIOH_API_SUCCESS(status) (status == 0)

#define SDIOH_READ              0	/* Read request */
#define SDIOH_WRITE             1	/* Write request */

#define SDIOH_DATA_FIX          0	/* Fixed addressing */
#define SDIOH_DATA_INC          1	/* Incremental addressing */

#define SDIOH_CMD_TYPE_NORMAL   0       /* Normal command */
#define SDIOH_CMD_TYPE_APPEND   1       /* Append command */
#define SDIOH_CMD_TYPE_CUTTHRU  2       /* Cut-through command */

#define SDIOH_DATA_PIO          0       /* PIO mode */
#define SDIOH_DATA_DMA          1       /* DMA mode */

#ifdef BCMSDIOH_TXGLOM
/* Max number of glommed pkts */
#ifdef CUSTOM_MAX_TXGLOM_SIZE
#define SDPCM_MAXGLOM_SIZE  CUSTOM_MAX_TXGLOM_SIZE
#else
#define SDPCM_MAXGLOM_SIZE	10
#endif /* CUSTOM_MAX_TXGLOM_SIZE */

#define SDPCM_TXGLOM_CPY 0			/* SDIO 2.0 should use copy mode */
#define SDPCM_TXGLOM_MDESC	1		/* SDIO 3.0 should use multi-desc mode */

#ifdef BCMSDIOH_TXGLOM_HIGHSPEED
#define SDPCM_DEFGLOM_MODE	SDPCM_TXGLOM_MDESC
#ifdef CUSTOM_DEF_TXGLOM_SIZE
#define SDPCM_DEFGLOM_SIZE  CUSTOM_DEF_TXGLOM_SIZE
#else
#define SDPCM_DEFGLOM_SIZE  10
#endif /* CUSTOM_DEF_TXGLOM_SIZE */
#else
#define SDPCM_DEFGLOM_MODE	SDPCM_TXGLOM_CPY
#define SDPCM_DEFGLOM_SIZE  3
#endif /* BCMSDIOH_TXGLOM_HIGHSPEED */

#if SDPCM_DEFGLOM_SIZE > SDPCM_MAXGLOM_SIZE
#warning "SDPCM_DEFGLOM_SIZE cannot be higher than SDPCM_MAXGLOM_SIZE!!"
#undef SDPCM_DEFGLOM_SIZE
#define SDPCM_DEFGLOM_SIZE SDPCM_MAXGLOM_SIZE
#endif
#endif /* BCMSDIOH_TXGLOM */

typedef int SDIOH_API_RC;

/* SDio Host structure */
typedef struct sdioh_info sdioh_info_t;

/* callback function, taking one arg */
typedef void (*sdioh_cb_fn_t)(void *);

/* attach, return handler on success, NULL if failed.
 *  The handler shall be provided by all subsequent calls. No local cache
 *  cfghdl points to the starting address of pci device mapped memory
 */
extern sdioh_info_t * sdioh_attach(osl_t *osh, void *cfghdl, uint irq);
extern SDIOH_API_RC sdioh_detach(osl_t *osh, sdioh_info_t *si);
extern SDIOH_API_RC sdioh_interrupt_register(sdioh_info_t *si, sdioh_cb_fn_t fn, void *argh);
extern SDIOH_API_RC sdioh_interrupt_deregister(sdioh_info_t *si);

/* query whether SD interrupt is enabled or not */
extern SDIOH_API_RC sdioh_interrupt_query(sdioh_info_t *si, bool *onoff);

/* enable or disable SD interrupt */
extern SDIOH_API_RC sdioh_interrupt_set(sdioh_info_t *si, bool enable_disable);

#if defined(DHD_DEBUG)
extern bool sdioh_interrupt_pending(sdioh_info_t *si);
#endif

/* read or write one byte using cmd52 */
extern SDIOH_API_RC sdioh_request_byte(sdioh_info_t *si, uint rw, uint fnc, uint addr, uint8 *byte);

/* read or write 2/4 bytes using cmd53 */
extern SDIOH_API_RC sdioh_request_word(sdioh_info_t *si, uint cmd_type, uint rw, uint fnc,
	uint addr, uint32 *word, uint nbyte);

/* read or write any buffer using cmd53 */
extern SDIOH_API_RC sdioh_request_buffer(sdioh_info_t *si, uint pio_dma, uint fix_inc,
	uint rw, uint fnc_num, uint32 addr, uint regwidth, uint32 buflen, uint8 *buffer,
	void *pkt);

#ifdef BCMSDIOH_TXGLOM
extern void	sdioh_glom_post(sdioh_info_t *sd, uint8 *frame, void *pkt, uint len);
extern void sdioh_glom_clear(sdioh_info_t *sd);
extern uint sdioh_set_mode(sdioh_info_t *sd, uint mode);
extern bool sdioh_glom_enabled(void);
#else
#define sdioh_glom_post(a, b, c, d)
#define sdioh_glom_clear(a)
#define sdioh_set_mode(a) (0)
#define sdioh_glom_enabled() (FALSE)
#endif

/* get cis data */
extern SDIOH_API_RC sdioh_cis_read(sdioh_info_t *si, uint fuc, uint8 *cis, uint32 length);

extern SDIOH_API_RC sdioh_cfg_read(sdioh_info_t *si, uint fuc, uint32 addr, uint8 *data);
extern SDIOH_API_RC sdioh_cfg_write(sdioh_info_t *si, uint fuc, uint32 addr, uint8 *data);

/* query number of io functions */
extern uint sdioh_query_iofnum(sdioh_info_t *si);

/* handle iovars */
extern int sdioh_iovar_op(sdioh_info_t *si, const char *name,
                          void *params, int plen, void *arg, int len, bool set);

/* Issue abort to the specified function and clear controller as needed */
extern int sdioh_abort(sdioh_info_t *si, uint fnc);

/* Start and Stop SDIO without re-enumerating the SD card. */
extern int sdioh_start(sdioh_info_t *si, int stage);
extern int sdioh_stop(sdioh_info_t *si);

/* Wait system lock free */
extern int sdioh_waitlockfree(sdioh_info_t *si);

/* Reset and re-initialize the device */
extern int sdioh_sdio_reset(sdioh_info_t *si);

/* Helper function */
void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh);



#if defined(BCMSDIOH_STD)
	#define SDIOH_SLEEP_ENABLED
#endif
extern SDIOH_API_RC sdioh_sleep(sdioh_info_t *si, bool enab);

/* GPIO support */
extern SDIOH_API_RC sdioh_gpio_init(sdioh_info_t *sd);
extern bool sdioh_gpioin(sdioh_info_t *sd, uint32 gpio);
extern SDIOH_API_RC sdioh_gpioouten(sdioh_info_t *sd, uint32 gpio);
extern SDIOH_API_RC sdioh_gpioout(sdioh_info_t *sd, uint32 gpio, bool enab);

#endif /* _sdio_api_h_ */
