OBJ = built-in.o

OBJS =

all: $(OBJ)

$(OBJ): $(OBJS)
	rm -f $@
	$(LD) $(LINKFLAGS) --start-group $^ --end-group -o $@

clean-files := $(OBJS) link.ld
