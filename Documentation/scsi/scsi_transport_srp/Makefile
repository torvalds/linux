all: rport_state_diagram.svg rport_state_diagram.png

rport_state_diagram.svg: rport_state_diagram.dot
	dot -Tsvg -o $@ $<

rport_state_diagram.png: rport_state_diagram.dot
	dot -Tpng -o $@ $<
