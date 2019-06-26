
SOURCES	:= $(wildcard [0-9]*x[0-9]*.S)

BIN	:= $(patsubst %.S, %.bin, $(SOURCES))

IHEX	:= $(patsubst %.S, %.bin.ihex, $(SOURCES))

CODE	:= $(patsubst %.S, %.c, $(SOURCES))

all:	$(BIN) $(IHEX) $(CODE)

clean:
	@rm -f *.o *.bin.ihex *.bin *.c

%.o:	%.S
	@cc -c $^

%.bin.nocrc:	%.o
	@objcopy -Obinary $^ $@

%.crc:	%.bin.nocrc
	@list=$$(for i in `seq 1 127`; do head -c$$i $^ | tail -c1 \
		| hexdump -v -e '/1 "%02X+"'; done); \
		echo "ibase=16;100-($${list%?})%100" | bc >$@

%.p:	%.crc %.S
	@cc -c -DCRC="$$(cat $*.crc)" -o $@ $*.S

%.bin:	%.p
	@objcopy -Obinary $^ $@

%.bin.ihex:	%.p
	@objcopy -Oihex $^ $@
	@dos2unix $@ 2>/dev/null

%.c:	%.bin
	@echo "{" >$@; hexdump -f hex $^ >>$@; echo "};" >>$@
