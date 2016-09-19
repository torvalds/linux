ifeq ($(strip $(V)), 1)
  Q =
else
  Q = @
endif

CFLAGS	+= -std=gnu99 -Wall -Wextra -g \
	    -D_GNU_SOURCE \
	    -Wno-unused-parameter \
	    -Wmaybe-uninitialized \
	    -Wredundant-decls \
	    -Wcast-align \
	    -Wsign-compare \
	    -Wno-missing-field-initializers

CC	:= $(CROSS_COMPILE)gcc

TOOLS = loopback_test

all: $(TOOLS)

%.o: %.c ../greybus_protocols.h
	@echo '  TARGET_CC $@'
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

loopback_%: loopback_%.o
	@echo '  TARGET_LD $@'
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean::
	rm -f *.o $(TOOLS)
