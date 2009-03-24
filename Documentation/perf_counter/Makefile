BINS = kerneltop perfstat

all: $(BINS)

kerneltop: kerneltop.c ../../include/linux/perf_counter.h
	cc -O6 -Wall -lrt -o $@ $<

perfstat: kerneltop
	ln -sf kerneltop perfstat

clean:
	rm $(BINS)
