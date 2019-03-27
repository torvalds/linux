.if ${OS_DISTRIBUTION} == "Ubuntu"
.if ${OS_DISTRIBUTION_VERSION} >= 14
# Ubuntu Trusty Tahr and later.

# Use the --nounput option to flex(1), to prevent unused functions from
# being generated.
LFLAGS += --nounput
.endif
.endif
