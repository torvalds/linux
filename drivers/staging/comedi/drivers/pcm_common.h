#ifndef _comedi_common_H
#define _comedi_common_H

extern int comedi_pcm_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd);

#endif
