# Generate the *.h.rst files from uAPI headers

PARSER = $(srctree)/Documentation/sphinx/parse-headers.pl
UAPI = $(srctree)/include/uapi/linux
KAPI = $(srctree)/include/linux
SRC_DIR=$(srctree)/Documentation/media

FILES = audio.h.rst ca.h.rst dmx.h.rst frontend.h.rst net.h.rst video.h.rst \
	  videodev2.h.rst media.h.rst cec.h.rst lirc.h.rst

TARGETS := $(addprefix $(BUILDDIR)/, $(FILES))

htmldocs: $(BUILDDIR) ${TARGETS}

$(BUILDDIR):
	$(Q)mkdir -p $@

# Rule to convert a .h file to inline RST documentation

gen_rst = \
	echo ${PARSER} $< $@ $(SRC_DIR)/$(notdir $@).exceptions; \
	${PARSER} $< $@ $(SRC_DIR)/$(notdir $@).exceptions

quiet_gen_rst = echo '  PARSE   $(patsubst $(srctree)/%,%,$<)'; \
	${PARSER} $< $@ $(SRC_DIR)/$(notdir $@).exceptions

silent_gen_rst = ${gen_rst}

$(BUILDDIR)/audio.h.rst: ${UAPI}/dvb/audio.h ${PARSER} $(SRC_DIR)/audio.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/ca.h.rst: ${UAPI}/dvb/ca.h ${PARSER} $(SRC_DIR)/ca.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/dmx.h.rst: ${UAPI}/dvb/dmx.h ${PARSER} $(SRC_DIR)/dmx.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/frontend.h.rst: ${UAPI}/dvb/frontend.h ${PARSER} $(SRC_DIR)/frontend.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/net.h.rst: ${UAPI}/dvb/net.h ${PARSER} $(SRC_DIR)/net.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/video.h.rst: ${UAPI}/dvb/video.h ${PARSER} $(SRC_DIR)/video.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/videodev2.h.rst: ${UAPI}/videodev2.h ${PARSER} $(SRC_DIR)/videodev2.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/media.h.rst: ${UAPI}/media.h ${PARSER} $(SRC_DIR)/media.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/cec.h.rst: ${KAPI}/cec.h ${PARSER} $(SRC_DIR)/cec.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/lirc.h.rst: ${UAPI}/lirc.h ${PARSER} $(SRC_DIR)/lirc.h.rst.exceptions
	@$($(quiet)gen_rst)

cleandocs:
	-rm ${TARGETS}
