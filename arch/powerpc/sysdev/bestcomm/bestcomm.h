/*
 * Public header for the MPC52xx processor BestComm driver
 *
 *
 * Copyright (C) 2006      Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2005      Varma Electronics Oy,
 *                         ( by Andrey Volkov <avolkov@varma-el.com> )
 * Copyright (C) 2003-2004 MontaVista, Software, Inc.
 *                         ( by Dale Farnsworth <dfarnsworth@mvista.com> )
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __BESTCOMM_H__
#define __BESTCOMM_H__

struct bcom_bd; /* defined later on ... */


/* ======================================================================== */
/* Generic task managment                                                   */
/* ======================================================================== */

/**
 * struct bcom_task - Structure describing a loaded BestComm task
 *
 * This structure is never built by the driver it self. It's built and
 * filled the intermediate layer of the BestComm API, the task dependent
 * support code.
 *
 * Most likely you don't need to poke around inside this structure. The
 * fields are exposed in the header just for the sake of inline functions
 */
struct bcom_task {
	unsigned int	tasknum;
	unsigned int	flags;
	int		irq;

	struct bcom_bd	*bd;
	phys_addr_t	bd_pa;
	void		**cookie;
	unsigned short	index;
	unsigned short	outdex;
	unsigned int	num_bd;
	unsigned int	bd_size;

	void*		priv;
};

#define BCOM_FLAGS_NONE         0x00000000ul
#define BCOM_FLAGS_ENABLE_TASK  (1ul <<  0)

/**
 * bcom_enable - Enable a BestComm task
 * @tsk: The BestComm task structure
 *
 * This function makes sure the given task is enabled and can be run
 * by the BestComm engine as needed
 */
extern void bcom_enable(struct bcom_task *tsk);

/**
 * bcom_disable - Disable a BestComm task
 * @tsk: The BestComm task structure
 *
 * This function disable a given task, making sure it's not executed
 * by the BestComm engine.
 */
extern void bcom_disable(struct bcom_task *tsk);


/**
 * bcom_get_task_irq - Returns the irq number of a BestComm task
 * @tsk: The BestComm task structure
 */
static inline int
bcom_get_task_irq(struct bcom_task *tsk) {
	return tsk->irq;
}

/* ======================================================================== */
/* BD based tasks helpers                                                   */
/* ======================================================================== */

/**
 * struct bcom_bd - Structure describing a generic BestComm buffer descriptor
 * @status: The current status of this buffer. Exact meaning depends on the
 *          task type
 * @data: An array of u32 whose meaning depends on the task type.
 */
struct bcom_bd {
	u32	status;
	u32	data[1];	/* variable, but at least 1 */
};

#define BCOM_BD_READY	0x40000000ul

/** _bcom_next_index - Get next input index.
 * @tsk: pointer to task structure
 *
 * Support function; Device drivers should not call this
 */
static inline int
_bcom_next_index(struct bcom_task *tsk)
{
	return ((tsk->index + 1) == tsk->num_bd) ? 0 : tsk->index + 1;
}

/** _bcom_next_outdex - Get next output index.
 * @tsk: pointer to task structure
 *
 * Support function; Device drivers should not call this
 */
static inline int
_bcom_next_outdex(struct bcom_task *tsk)
{
	return ((tsk->outdex + 1) == tsk->num_bd) ? 0 : tsk->outdex + 1;
}

/**
 * bcom_queue_empty - Checks if a BestComm task BD queue is empty
 * @tsk: The BestComm task structure
 */
static inline int
bcom_queue_empty(struct bcom_task *tsk)
{
	return tsk->index == tsk->outdex;
}

/**
 * bcom_queue_full - Checks if a BestComm task BD queue is full
 * @tsk: The BestComm task structure
 */
static inline int
bcom_queue_full(struct bcom_task *tsk)
{
	return tsk->outdex == _bcom_next_index(tsk);
}

/**
 * bcom_buffer_done - Checks if a BestComm 
 * @tsk: The BestComm task structure
 */
static inline int
bcom_buffer_done(struct bcom_task *tsk)
{
	if (bcom_queue_empty(tsk))
		return 0;
	return !(tsk->bd[tsk->outdex].status & BCOM_BD_READY);
}

/**
 * bcom_prepare_next_buffer - clear status of next available buffer.
 * @tsk: The BestComm task structure
 *
 * Returns pointer to next buffer descriptor
 */
static inline struct bcom_bd *
bcom_prepare_next_buffer(struct bcom_task *tsk)
{
	tsk->bd[tsk->index].status = 0;	/* cleanup last status */
	return &tsk->bd[tsk->index];
}

static inline void
bcom_submit_next_buffer(struct bcom_task *tsk, void *cookie)
{
	tsk->cookie[tsk->index] = cookie;
	mb();	/* ensure the bd is really up-to-date */
	tsk->bd[tsk->index].status |= BCOM_BD_READY;
	tsk->index = _bcom_next_index(tsk);
	if (tsk->flags & BCOM_FLAGS_ENABLE_TASK)
		bcom_enable(tsk);
}

static inline void *
bcom_retrieve_buffer(struct bcom_task *tsk, u32 *p_status, struct bcom_bd **p_bd)
{
	void *cookie = tsk->cookie[tsk->outdex];
	if (p_status)
		*p_status = tsk->bd[tsk->outdex].status;
	if (p_bd)
		*p_bd = &tsk->bd[tsk->outdex];
	tsk->outdex = _bcom_next_outdex(tsk);
	return cookie;
}

#endif /* __BESTCOMM_H__ */
