# $Id: phony-end.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

all ok also.ok bug phony:
	@echo '${.TARGET .PREFIX .IMPSRC:L:@v@$v="${$v}"@}'

.END:	ok also.ok bug

phony bug:	.PHONY
all: phony
