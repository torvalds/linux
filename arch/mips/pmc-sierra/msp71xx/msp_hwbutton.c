/*
 * Sets up interrupt handlers for various hardware switches which are
 * connected to interrupt lines.
 *
 * Copyright 2005-2207 PMC-Sierra, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <msp_int.h>
#include <msp_regs.h>
#include <msp_regops.h>
#ifdef CONFIG_PMCTWILED
#include <msp_led_macros.h>
#endif

/* For hwbutton_interrupt->initial_state */
#define HWBUTTON_HI	0x1
#define HWBUTTON_LO	0x2

/*
 * This struct describes a hardware button
 */
struct hwbutton_interrupt {
	char *name;			/* Name of button */
	int irq;			/* Actual LINUX IRQ */
	int eirq;			/* Extended IRQ number (0-7) */
	int initial_state;		/* The "normal" state of the switch */
	void (*handle_hi)(void *);	/* Handler: switch input has gone HI */
	void (*handle_lo)(void *);	/* Handler: switch input has gone LO */
	void *data;			/* Optional data to pass to handler */
};

#ifdef CONFIG_PMC_MSP7120_GW
extern void msp_restart(char *);

static void softreset_push(void *data)
{
	printk(KERN_WARNING "SOFTRESET switch was pushed\n");

	/*
	 * In the future you could move this to the release handler,
	 * timing the difference between the 'push' and 'release', and only
	 * doing this ungraceful restart if the button has been down for
	 * a certain amount of time; otherwise doing a graceful restart.
	 */

	msp_restart(NULL);
}

static void softreset_release(void *data)
{
	printk(KERN_WARNING "SOFTRESET switch was released\n");

	/* Do nothing */
}

static void standby_on(void *data)
{
	printk(KERN_WARNING "STANDBY switch was set to ON (not implemented)\n");

	/* TODO: Put board in standby mode */
#ifdef CONFIG_PMCTWILED
	msp_led_turn_off(MSP_LED_PWRSTANDBY_GREEN);
	msp_led_turn_on(MSP_LED_PWRSTANDBY_RED);
#endif
}

static void standby_off(void *data)
{
	printk(KERN_WARNING
		"STANDBY switch was set to OFF (not implemented)\n");

	/* TODO: Take out of standby mode */
#ifdef CONFIG_PMCTWILED
	msp_led_turn_on(MSP_LED_PWRSTANDBY_GREEN);
	msp_led_turn_off(MSP_LED_PWRSTANDBY_RED);
#endif
}

static struct hwbutton_interrupt softreset_sw = {
	.name = "Softreset button",
	.irq = MSP_INT_EXT0,
	.eirq = 0,
	.initial_state = HWBUTTON_HI,
	.handle_hi = softreset_release,
	.handle_lo = softreset_push,
	.data = NULL,
};

static struct hwbutton_interrupt standby_sw = {
	.name = "Standby switch",
	.irq = MSP_INT_EXT1,
	.eirq = 1,
	.initial_state = HWBUTTON_HI,
	.handle_hi = standby_off,
	.handle_lo = standby_on,
	.data = NULL,
};
#endif /* CONFIG_PMC_MSP7120_GW */

static irqreturn_t hwbutton_handler(int irq, void *data)
{
	struct hwbutton_interrupt *hirq = data;
	unsigned long cic_ext = *CIC_EXT_CFG_REG;

	if (irq != hirq->irq)
		return IRQ_NONE;

	if (CIC_EXT_IS_ACTIVE_HI(cic_ext, hirq->eirq)) {
		/* Interrupt: pin is now HI */
		CIC_EXT_SET_ACTIVE_LO(cic_ext, hirq->eirq);
		hirq->handle_hi(hirq->data);
	} else {
		/* Interrupt: pin is now LO */
		CIC_EXT_SET_ACTIVE_HI(cic_ext, hirq->eirq);
		hirq->handle_lo(hirq->data);
	}

	/*
	 * Invert the POLARITY of this level interrupt to ack the interrupt
	 * Thus next state change will invoke the opposite message
	 */
	*CIC_EXT_CFG_REG = cic_ext;

	return IRQ_HANDLED;
}

static int msp_hwbutton_register(struct hwbutton_interrupt *hirq)
{
	unsigned long cic_ext;

	if (hirq->handle_hi == NULL || hirq->handle_lo == NULL)
		return -EINVAL;

	cic_ext = *CIC_EXT_CFG_REG;
	CIC_EXT_SET_TRIGGER_LEVEL(cic_ext, hirq->eirq);
	if (hirq->initial_state == HWBUTTON_HI)
		CIC_EXT_SET_ACTIVE_LO(cic_ext, hirq->eirq);
	else
		CIC_EXT_SET_ACTIVE_HI(cic_ext, hirq->eirq);
	*CIC_EXT_CFG_REG = cic_ext;

	return request_irq(hirq->irq, hwbutton_handler, IRQF_DISABLED,
				hirq->name, (void *)hirq);
}

static int __init msp_hwbutton_setup(void)
{
#ifdef CONFIG_PMC_MSP7120_GW
	msp_hwbutton_register(&softreset_sw);
	msp_hwbutton_register(&standby_sw);
#endif
	return 0;
}

subsys_initcall(msp_hwbutton_setup);
