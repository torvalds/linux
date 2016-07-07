# Generate the *.h.rst files from uAPI headers

PARSER = ../sphinx/parse-headers.pl
UAPI = ../../include/uapi/linux

htmldocs: audio.h.rst dmx.h.rst frontend.h.rst

audio.h.rst: ${PARSER} ${UAPI}/dvb/audio.h  audio.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/audio.h $@ audio.h.rst.exceptions

dmx.h.rst: ${PARSER} ${UAPI}/dvb/dmx.h  dmx.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/dmx.h $@ dmx.h.rst.exceptions

frontend.h.rst: ${PARSER} ${UAPI}/dvb/frontend.h  frontend.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/frontend.h $@ frontend.h.rst.exceptions

clean:
	-rm frontend.h.rst
