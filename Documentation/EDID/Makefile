
SOURCES	:= $(wildcard [0-9]*x[0-9]*.S)

BIN	:= $(patsubst %.S, %.bin, $(SOURCES))

IHEX	:= $(patsubst %.S, %.bin.ihex, $(SOURCES))

CODE	:= $(patsubst %.S, %.c, $(SOURCES))

all:	$(BIN) $(IHEX) $(CODE)

clean:
	@rm -f *.o *.bin.ihex *.bin *.c

%.o:	%.S
	@cc -c $^

%.bin:	%.o
	@objcopy -Obinary $^ $@

%.bin.ihex:	%.o
	@objcopy -Oihex $^ $@
	@dos2unix $@ 2>/dev/null

%.c:	%.bin
	@echo "{" >$@; hexdump -f hex $^ >>$@; echo "};" >>$@
