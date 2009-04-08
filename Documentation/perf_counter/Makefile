BINS = kerneltop perfstat perf-record perf-report

all: $(BINS)

kerneltop: kerneltop.c ../../include/linux/perf_counter.h
	cc -O6 -Wall -lrt -o $@ $<

perf-record: perf-record.c ../../include/linux/perf_counter.h
	cc -O6 -Wall -lrt -o $@ $<

perf-report: perf-report.cc ../../include/linux/perf_counter.h
	g++ -O6 -Wall -lrt -o $@ $<

perfstat: kerneltop
	ln -sf kerneltop perfstat

clean:
	rm $(BINS)
