/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/interrupt.h>

#include "./common.h"
#include "./mod.h"

#define usbhs_priv_to_modinfo(priv) (&priv->mod_info)
#define usbhs_mod_info_call(priv, func, param...)	\
({						\
	struct usbhs_mod_info *info;		\
	info = usbhs_priv_to_modinfo(priv);	\
	!info->func ? 0 :			\
	 info->func(param);			\
})

/*
 *		autonomy
 *
 * these functions are used if platform doesn't have external phy.
 *  -> there is no "notify_hotplug" callback from platform
 *  -> call "notify_hotplug" by itself
 *  -> use own interrupt to connect/disconnect
 *  -> it mean module clock is always ON
 *             ~~~~~~~~~~~~~~~~~~~~~~~~~
 */
static int usbhsm_autonomy_get_vbus(struct platform_device *pdev)
{
	struct usbhs_priv *priv = usbhs_pdev_to_priv(pdev);

	return  VBSTS & usbhs_read(priv, INTSTS0);
}

static int usbhsm_autonomy_irq_vbus(struct usbhs_priv *priv,
				    struct usbhs_irq_state *irq_state)
{
	struct platform_device *pdev = usbhs_priv_to_pdev(priv);

	renesas_usbhs_call_notify_hotplug(pdev);

	return 0;
}

void usbhs_mod_autonomy_mode(struct usbhs_priv *priv)
{
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);

	info->irq_vbus		= usbhsm_autonomy_irq_vbus;
	priv->pfunc.get_vbus	= usbhsm_autonomy_get_vbus;

	usbhs_irq_callback_update(priv, NULL);
}

/*
 *		host / gadget functions
 *
 * renesas_usbhs host/gadget can register itself by below functions.
 * these functions are called when probe
 *
 */
void usbhs_mod_register(struct usbhs_priv *priv, struct usbhs_mod *mod, int id)
{
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);

	info->mod[id]	= mod;
	mod->priv	= priv;
}

struct usbhs_mod *usbhs_mod_get(struct usbhs_priv *priv, int id)
{
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);
	struct usbhs_mod *ret = NULL;

	switch (id) {
	case USBHS_HOST:
	case USBHS_GADGET:
		ret = info->mod[id];
		break;
	}

	return ret;
}

int usbhs_mod_is_host(struct usbhs_priv *priv)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);

	if (!mod)
		return -EINVAL;

	return info->mod[USBHS_HOST] == mod;
}

struct usbhs_mod *usbhs_mod_get_current(struct usbhs_priv *priv)
{
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);

	return info->curt;
}

int usbhs_mod_change(struct usbhs_priv *priv, int id)
{
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);
	struct usbhs_mod *mod = NULL;
	int ret = 0;

	/* id < 0 mean no current */
	switch (id) {
	case USBHS_HOST:
	case USBHS_GADGET:
		mod = info->mod[id];
		break;
	default:
		ret = -EINVAL;
	}
	info->curt = mod;

	return ret;
}

static irqreturn_t usbhs_interrupt(int irq, void *data);
int usbhs_mod_probe(struct usbhs_priv *priv)
{
	struct device *dev = usbhs_priv_to_dev(priv);
	int ret;

	/*
	 * install host/gadget driver
	 */
	ret = usbhs_mod_host_probe(priv);
	if (ret < 0)
		return ret;

	ret = usbhs_mod_gadget_probe(priv);
	if (ret < 0)
		goto mod_init_host_err;

	/* irq settings */
	ret = request_irq(priv->irq, usbhs_interrupt,
			  0, dev_name(dev), priv);
	if (ret) {
		dev_err(dev, "irq request err\n");
		goto mod_init_gadget_err;
	}

	return ret;

mod_init_gadget_err:
	usbhs_mod_gadget_remove(priv);
mod_init_host_err:
	usbhs_mod_host_remove(priv);

	return ret;
}

void usbhs_mod_remove(struct usbhs_priv *priv)
{
	usbhs_mod_host_remove(priv);
	usbhs_mod_gadget_remove(priv);
	free_irq(priv->irq, priv);
}

/*
 *		status functions
 */
int usbhs_status_get_device_state(struct usbhs_irq_state *irq_state)
{
	int state = irq_state->intsts0 & DVSQ_MASK;

	switch (state) {
	case POWER_STATE:
	case DEFAULT_STATE:
	case ADDRESS_STATE:
	case CONFIGURATION_STATE:
		return state;
	}

	return -EIO;
}

int usbhs_status_get_ctrl_stage(struct usbhs_irq_state *irq_state)
{
	/*
	 * return value
	 *
	 * IDLE_SETUP_STAGE
	 * READ_DATA_STAGE
	 * READ_STATUS_STAGE
	 * WRITE_DATA_STAGE
	 * WRITE_STATUS_STAGE
	 * NODATA_STATUS_STAGE
	 * SEQUENCE_ERROR
	 */
	return (int)irq_state->intsts0 & CTSQ_MASK;
}

static void usbhs_status_get_each_irq(struct usbhs_priv *priv,
				      struct usbhs_irq_state *state)
{
	struct usbhs_mod *mod = usbhs_mod_get_current(priv);

	state->intsts0 = usbhs_read(priv, INTSTS0);
	state->intsts1 = usbhs_read(priv, INTSTS1);

	/* mask */
	if (mod) {
		state->brdysts = usbhs_read(priv, BRDYSTS);
		state->nrdysts = usbhs_read(priv, NRDYSTS);
		state->bempsts = usbhs_read(priv, BEMPSTS);

		state->bempsts &= mod->irq_bempsts;
		state->brdysts &= mod->irq_brdysts;
	}
}

/*
 *		interrupt
 */
#define INTSTS0_MAGIC 0xF800 /* acknowledge magical interrupt sources */
#define INTSTS1_MAGIC 0xA870 /* acknowledge magical interrupt sources */
static irqreturn_t usbhs_interrupt(int irq, void *data)
{
	struct usbhs_priv *priv = data;
	struct usbhs_irq_state irq_state;

	usbhs_status_get_each_irq(priv, &irq_state);

	/*
	 * clear interrupt
	 *
	 * The hardware is _very_ picky to clear interrupt bit.
	 * Especially INTSTS0_MAGIC, INTSTS1_MAGIC value.
	 *
	 * see
	 *	"Operation"
	 *	 - "Control Transfer (DCP)"
	 *	   - Function :: VALID bit should 0
	 */
	usbhs_write(priv, INTSTS0, ~irq_state.intsts0 & INTSTS0_MAGIC);
	usbhs_write(priv, INTSTS1, ~irq_state.intsts1 & INTSTS1_MAGIC);

	usbhs_write(priv, BRDYSTS, 0);
	usbhs_write(priv, NRDYSTS, 0);
	usbhs_write(priv, BEMPSTS, 0);

	/*
	 * call irq callback functions
	 * see also
	 *	usbhs_irq_setting_update
	 */

	/* INTSTS0 */
	if (irq_state.intsts0 & VBINT)
		usbhs_mod_info_call(priv, irq_vbus, priv, &irq_state);

	if (irq_state.intsts0 & DVST)
		usbhs_mod_call(priv, irq_dev_state, priv, &irq_state);

	if (irq_state.intsts0 & CTRT)
		usbhs_mod_call(priv, irq_ctrl_stage, priv, &irq_state);

	if (irq_state.intsts0 & BEMP)
		usbhs_mod_call(priv, irq_empty, priv, &irq_state);

	if (irq_state.intsts0 & BRDY)
		usbhs_mod_call(priv, irq_ready, priv, &irq_state);

	/* INTSTS1 */
	if (irq_state.intsts1 & ATTCH)
		usbhs_mod_call(priv, irq_attch, priv, &irq_state);

	if (irq_state.intsts1 & DTCH)
		usbhs_mod_call(priv, irq_dtch, priv, &irq_state);

	if (irq_state.intsts1 & SIGN)
		usbhs_mod_call(priv, irq_sign, priv, &irq_state);

	if (irq_state.intsts1 & SACK)
		usbhs_mod_call(priv, irq_sack, priv, &irq_state);

	return IRQ_HANDLED;
}

void usbhs_irq_callback_update(struct usbhs_priv *priv, struct usbhs_mod *mod)
{
	u16 intenb0 = 0;
	u16 intenb1 = 0;
	struct usbhs_mod_info *info = usbhs_priv_to_modinfo(priv);

	/*
	 * BEMPENB/BRDYENB are picky.
	 * below method is required
	 *
	 *  - clear  INTSTS0
	 *  - update BEMPENB/BRDYENB
	 *  - update INTSTS0
	 */
	usbhs_write(priv, INTENB0, 0);
	usbhs_write(priv, INTENB1, 0);

	usbhs_write(priv, BEMPENB, 0);
	usbhs_write(priv, BRDYENB, 0);

	/*
	 * see also
	 *	usbhs_interrupt
	 */

	/*
	 * it don't enable DVSE (intenb0) here
	 * but "mod->irq_dev_state" will be called.
	 */
	if (info->irq_vbus)
		intenb0 |= VBSE;

	if (mod) {
		/*
		 * INTSTS0
		 */
		if (mod->irq_ctrl_stage)
			intenb0 |= CTRE;

		if (mod->irq_empty && mod->irq_bempsts) {
			usbhs_write(priv, BEMPENB, mod->irq_bempsts);
			intenb0 |= BEMPE;
		}

		if (mod->irq_ready && mod->irq_brdysts) {
			usbhs_write(priv, BRDYENB, mod->irq_brdysts);
			intenb0 |= BRDYE;
		}

		/*
		 * INTSTS1
		 */
		if (mod->irq_attch)
			intenb1 |= ATTCHE;

		if (mod->irq_dtch)
			intenb1 |= DTCHE;

		if (mod->irq_sign)
			intenb1 |= SIGNE;

		if (mod->irq_sack)
			intenb1 |= SACKE;
	}

	if (intenb0)
		usbhs_write(priv, INTENB0, intenb0);

	if (intenb1)
		usbhs_write(priv, INTENB1, intenb1);
}
