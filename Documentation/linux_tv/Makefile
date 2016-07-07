# Generate the *.h.rst files from uAPI headers

PARSER = ../sphinx/parse-headers.pl
UAPI = ../../include/uapi/linux
TARGETS = audio.h.rst ca.h.rst dmx.h.rst frontend.h.rst net.h.rst

htmldocs: ${TARGETS}

audio.h.rst: ${PARSER} ${UAPI}/dvb/audio.h  audio.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/audio.h $@ audio.h.rst.exceptions

ca.h.rst: ${PARSER} ${UAPI}/dvb/ca.h  ca.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/ca.h $@ ca.h.rst.exceptions

dmx.h.rst: ${PARSER} ${UAPI}/dvb/dmx.h  dmx.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/dmx.h $@ dmx.h.rst.exceptions

frontend.h.rst: ${PARSER} ${UAPI}/dvb/frontend.h  frontend.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/frontend.h $@ frontend.h.rst.exceptions

net.h.rst: ${PARSER} ${UAPI}/dvb/net.h  net.h.rst.exceptions
	${PARSER} ${UAPI}/dvb/net.h $@ net.h.rst.exceptions

cleandocs:
	-rm ${TARGETS}
