#ifndef _ASM_UAPI_LKL_IRQ_H
#define _ASM_UAPI_LKL_IRQ_H

/**
 * lkl_trigger_irq - generate an interrupt
 *
 * This function is used by the device host side to signal its Linux counterpart
 * that some event happened.
 *
 * @irq - the irq number to signal
 */
int lkl_trigger_irq(int irq);

/**
 * lkl_get_free_irq - find and reserve a free IRQ number
 *
 * This function is called by the host device code to find an unused IRQ number
 * and reserved it for its own use.
 *
 * @user - a string to identify the user
 * @returns - and irq number that can be used by request_irq or an negative
 * value in case of an error
 */
int lkl_get_free_irq(const char *user);

/**
 * lkl_put_irq - release an IRQ number previously obtained with lkl_get_free_irq
 *
 * @irq - irq number to release
 * @user - string identifying the user; should be the same as the one passed to
 * lkl_get_free_irq when the irq number was obtained
 */
void lkl_put_irq(int irq, const char *name);

#endif
