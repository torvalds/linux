#include "../comedidev.h"

#include "comedi_fc.h"
#include "pcm_common.h"

int comedi_pcm_cmdtest(struct comedi_device *dev,
		       struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	/* cmd->start_src == TRIG_NOW || cmd->start_src == TRIG_INT */
	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	/* cmd->scan_begin_src == TRIG_EXT */
	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}

	/* cmd->convert_src == TRIG_NOW */
	if (cmd->convert_arg != 0) {
		cmd->convert_arg = 0;
		err++;
	}

	/* cmd->scan_end_src == TRIG_COUNT */
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/* any count allowed */
		break;
	case TRIG_NONE:
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* if (err) return 4; */

	return 0;
}
EXPORT_SYMBOL(comedi_pcm_cmdtest);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
