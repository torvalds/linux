;; AMD Athlon Scheduling
;;
;; The Athlon does contain three pipelined FP units, three integer units and
;; three address generation units. 
;;
;; The predecode logic is determining boundaries of instructions in the 64
;; byte cache line. So the cache line straddling problem of K6 might be issue
;; here as well, but it is not noted in the documentation.
;;
;; Three DirectPath instructions decoders and only one VectorPath decoder
;; is available. They can decode three DirectPath instructions or one VectorPath
;; instruction per cycle.
;; Decoded macro instructions are then passed to 72 entry instruction control
;; unit, that passes
;; it to the specialized integer (18 entry) and fp (36 entry) schedulers.
;;
;; The load/store queue unit is not attached to the schedulers but
;; communicates with all the execution units separately instead.

(define_attr "athlon_decode" "direct,vector,double"
  (cond [(eq_attr "type" "call,imul,idiv,other,multi,fcmov,fpspc,str,pop,cld,leave")
	   (const_string "vector")
         (and (eq_attr "type" "push")
              (match_operand 1 "memory_operand" ""))
	   (const_string "vector")
         (and (eq_attr "type" "fmov")
	      (and (eq_attr "memory" "load,store")
		   (eq_attr "mode" "XF")))
	   (const_string "vector")]
	(const_string "direct")))

(define_attr "amdfam10_decode" "direct,vector,double"
  (const_string "direct"))
;;
;;           decode0 decode1 decode2
;;                 \    |   /
;;    instruction control unit (72 entry scheduler)
;;                |                        |
;;      integer scheduler (18)         stack map
;;     /  |    |    |    |   \        stack rename
;;  ieu0 agu0 ieu1 agu1 ieu2 agu2      scheduler
;;    |  agu0  |   agu1      agu2    register file
;;    |      \ |    |       /         |     |     |
;;     \      /\    |     /         fadd  fmul  fstore
;;       \  /    \  |   /           fadd  fmul  fstore
;;       imul  load/store (2x)      fadd  fmul  fstore

(define_automaton "athlon,athlon_load,athlon_mult,athlon_fp")
(define_cpu_unit "athlon-decode0" "athlon")
(define_cpu_unit "athlon-decode1" "athlon")
(define_cpu_unit "athlon-decode2" "athlon")
(define_cpu_unit "athlon-decodev" "athlon")
;; Model the fact that double decoded instruction may take 2 cycles
;; to decode when decoder2 and decoder0 in next cycle
;; is used (this is needed to allow troughput of 1.5 double decoded
;; instructions per cycle).
;;
;; In order to avoid dependence between reservation of decoder
;; and other units, we model decoder as two stage fully pipelined unit
;; and only double decoded instruction may occupy unit in the first cycle.
;; With this scheme however two double instructions can be issued cycle0.
;;
;; Avoid this by using presence set requiring decoder0 to be allocated
;; too. Vector decoded instructions then can't be issued when
;; modeled as consuming decoder0+decoder1+decoder2.
;; We solve that by specialized vector decoder unit and exclusion set.
(presence_set "athlon-decode2" "athlon-decode0")
(exclusion_set "athlon-decodev" "athlon-decode0,athlon-decode1,athlon-decode2")
(define_reservation "athlon-vector" "nothing,athlon-decodev")
(define_reservation "athlon-direct0" "nothing,athlon-decode0")
(define_reservation "athlon-direct" "nothing,
				     (athlon-decode0 | athlon-decode1
				     | athlon-decode2)")
;; Double instructions behaves like two direct instructions.
(define_reservation "athlon-double" "((athlon-decode2, athlon-decode0)
				     | (nothing,(athlon-decode0 + athlon-decode1))
				     | (nothing,(athlon-decode1 + athlon-decode2)))")

;; Agu and ieu unit results in extremely large automatons and
;; in our approximation they are hardly filled in.  Only ieu
;; unit can, as issue rate is 3 and agu unit is always used
;; first in the insn reservations.  Skip the models.

;(define_cpu_unit "athlon-ieu0" "athlon_ieu")
;(define_cpu_unit "athlon-ieu1" "athlon_ieu")
;(define_cpu_unit "athlon-ieu2" "athlon_ieu")
;(define_reservation "athlon-ieu" "(athlon-ieu0 | athlon-ieu1 | athlon-ieu2)")
(define_reservation "athlon-ieu" "nothing")
(define_cpu_unit "athlon-ieu0" "athlon")
;(define_cpu_unit "athlon-agu0" "athlon_agu")
;(define_cpu_unit "athlon-agu1" "athlon_agu")
;(define_cpu_unit "athlon-agu2" "athlon_agu")
;(define_reservation "athlon-agu" "(athlon-agu0 | athlon-agu1 | athlon-agu2)")
(define_reservation "athlon-agu" "nothing")

(define_cpu_unit "athlon-mult" "athlon_mult")

(define_cpu_unit "athlon-load0" "athlon_load")
(define_cpu_unit "athlon-load1" "athlon_load")
(define_reservation "athlon-load" "athlon-agu,
				   (athlon-load0 | athlon-load1),nothing")
;; 128bit SSE instructions issue two loads at once
(define_reservation "athlon-load2" "athlon-agu,
				   (athlon-load0 + athlon-load1),nothing")

(define_reservation "athlon-store" "(athlon-load0 | athlon-load1)")
;; 128bit SSE instructions issue two stores at once
(define_reservation "athlon-store2" "(athlon-load0 + athlon-load1)")


;; The FP operations start to execute at stage 12 in the pipeline, while
;; integer operations start to execute at stage 9 for Athlon and 11 for K8
;; Compensate the difference for Athlon because it results in significantly
;; smaller automata.
(define_reservation "athlon-fpsched" "nothing,nothing,nothing")
;; The floating point loads.
(define_reservation "athlon-fpload" "(athlon-fpsched + athlon-load)")
(define_reservation "athlon-fpload2" "(athlon-fpsched + athlon-load2)")
(define_reservation "athlon-fploadk8" "(athlon-fpsched + athlon-load)")
(define_reservation "athlon-fpload2k8" "(athlon-fpsched + athlon-load2)")


;; The three fp units are fully pipelined with latency of 3
(define_cpu_unit "athlon-fadd" "athlon_fp")
(define_cpu_unit "athlon-fmul" "athlon_fp")
(define_cpu_unit "athlon-fstore" "athlon_fp")
(define_reservation "athlon-fany" "(athlon-fstore | athlon-fmul | athlon-fadd)")
(define_reservation "athlon-faddmul" "(athlon-fadd | athlon-fmul)")

;; Vector operations usually consume many of pipes.
(define_reservation "athlon-fvector" "(athlon-fadd + athlon-fmul + athlon-fstore)")


;; Jump instructions are executed in the branch unit completely transparent to us
(define_insn_reservation "athlon_branch" 0
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "ibr"))
			 "athlon-direct,athlon-ieu")
(define_insn_reservation "athlon_call" 0
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "type" "call,callv"))
			 "athlon-vector,athlon-ieu")
(define_insn_reservation "athlon_call_amdfam10" 0
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "call,callv"))
			 "athlon-double,athlon-ieu")

;; Latency of push operation is 3 cycles, but ESP value is available
;; earlier
(define_insn_reservation "athlon_push" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "push"))
			 "athlon-direct,athlon-agu,athlon-store")
(define_insn_reservation "athlon_pop" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "type" "pop"))
			 "athlon-vector,athlon-load,athlon-ieu")
(define_insn_reservation "athlon_pop_k8" 3
			 (and (eq_attr "cpu" "k8,generic64")
			      (eq_attr "type" "pop"))
			 "athlon-double,(athlon-ieu+athlon-load)")
(define_insn_reservation "athlon_pop_amdfam10" 3
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "pop"))
			 "athlon-direct,(athlon-ieu+athlon-load)")
(define_insn_reservation "athlon_leave" 3
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "leave"))
			 "athlon-vector,(athlon-ieu+athlon-load)")
(define_insn_reservation "athlon_leave_k8" 3
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (eq_attr "type" "leave"))
			 "athlon-double,(athlon-ieu+athlon-load)")

;; Lea executes in AGU unit with 2 cycles latency.
(define_insn_reservation "athlon_lea" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "type" "lea"))
			 "athlon-direct,athlon-agu,nothing")
;; Lea executes in AGU unit with 1 cycle latency on AMDFAM10
(define_insn_reservation "athlon_lea_amdfam10" 1
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "lea"))
			 "athlon-direct,athlon-agu,nothing")

;; Mul executes in special multiplier unit attached to IEU0
(define_insn_reservation "athlon_imul" 5
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "imul")
				   (eq_attr "memory" "none,unknown")))
			 "athlon-vector,athlon-ieu0,athlon-mult,nothing,nothing,athlon-ieu0")
;; ??? Widening multiply is vector or double.
(define_insn_reservation "athlon_imul_k8_DI" 4
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "imul")
				   (and (eq_attr "mode" "DI")
					(eq_attr "memory" "none,unknown"))))
			 "athlon-direct0,athlon-ieu0,athlon-mult,nothing,athlon-ieu0")
(define_insn_reservation "athlon_imul_k8" 3
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "imul")
				   (eq_attr "memory" "none,unknown")))
			 "athlon-direct0,athlon-ieu0,athlon-mult,athlon-ieu0")
(define_insn_reservation "athlon_imul_amdfam10_HI" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "imul")
				   (and (eq_attr "mode" "HI")
					(eq_attr "memory" "none,unknown"))))
			 "athlon-vector,athlon-ieu0,athlon-mult,nothing,athlon-ieu0")			 
(define_insn_reservation "athlon_imul_mem" 8
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "imul")
				   (eq_attr "memory" "load,both")))
			 "athlon-vector,athlon-load,athlon-ieu,athlon-mult,nothing,nothing,athlon-ieu")
(define_insn_reservation "athlon_imul_mem_k8_DI" 7
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "imul")
				   (and (eq_attr "mode" "DI")
					(eq_attr "memory" "load,both"))))
			 "athlon-vector,athlon-load,athlon-ieu,athlon-mult,nothing,athlon-ieu")
(define_insn_reservation "athlon_imul_mem_k8" 6
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "imul")
				   (eq_attr "memory" "load,both")))
			 "athlon-vector,athlon-load,athlon-ieu,athlon-mult,athlon-ieu")

;; Idiv cannot execute in parallel with other instructions.  Dealing with it
;; as with short latency vector instruction is good approximation avoiding
;; scheduler from trying too hard to can hide it's latency by overlap with
;; other instructions.
;; ??? Experiments show that the idiv can overlap with roughly 6 cycles
;; of the other code
;; Using the same heuristics for amdfam10 as K8 with idiv

(define_insn_reservation "athlon_idiv" 6
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "idiv")
				   (eq_attr "memory" "none,unknown")))
			 "athlon-vector,(athlon-ieu0*6+(athlon-fpsched,athlon-fvector))")
(define_insn_reservation "athlon_idiv_mem" 9
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "idiv")
				   (eq_attr "memory" "load,both")))
			 "athlon-vector,((athlon-load,athlon-ieu0*6)+(athlon-fpsched,athlon-fvector))")
;; The parallelism of string instructions is not documented.  Model it same way
;; as idiv to create smaller automata.  This probably does not matter much.
;; Using the same heuristics for amdfam10 as K8 with idiv
(define_insn_reservation "athlon_str" 6
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "str")
				   (eq_attr "memory" "load,both,store")))
			 "athlon-vector,athlon-load,athlon-ieu0*6")

(define_insn_reservation "athlon_idirect" 1
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "none,unknown"))))
			 "athlon-direct,athlon-ieu")
(define_insn_reservation "athlon_idirect_amdfam10" 1
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "none,unknown"))))
			 "athlon-direct,athlon-ieu")
(define_insn_reservation "athlon_ivector" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "none,unknown"))))
			 "athlon-vector,athlon-ieu,athlon-ieu")
(define_insn_reservation "athlon_ivector_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "none,unknown"))))
			 "athlon-vector,athlon-ieu,athlon-ieu")

(define_insn_reservation "athlon_idirect_loadmov" 3
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "imov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-load")

(define_insn_reservation "athlon_idirect_load" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-load,athlon-ieu")
(define_insn_reservation "athlon_idirect_load_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-load,athlon-ieu")
(define_insn_reservation "athlon_ivector_load" 6
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "load"))))
			 "athlon-vector,athlon-load,athlon-ieu,athlon-ieu")
(define_insn_reservation "athlon_ivector_load_amdfam10" 6
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "load"))))
			 "athlon-vector,athlon-load,athlon-ieu,athlon-ieu")

(define_insn_reservation "athlon_idirect_movstore" 1
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "imov")
				   (eq_attr "memory" "store")))
			 "athlon-direct,athlon-agu,athlon-store")

(define_insn_reservation "athlon_idirect_both" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "both"))))
			 "athlon-direct,athlon-load,
			  athlon-ieu,athlon-store,
			  athlon-store")
(define_insn_reservation "athlon_idirect_both_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "both"))))
			 "athlon-direct,athlon-load,
			  athlon-ieu,athlon-store,
			  athlon-store")			  

(define_insn_reservation "athlon_ivector_both" 6
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "both"))))
			 "athlon-vector,athlon-load,
			  athlon-ieu,
			  athlon-ieu,
			  athlon-store")
(define_insn_reservation "athlon_ivector_both_amdfam10" 6
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "both"))))
			 "athlon-vector,athlon-load,
			  athlon-ieu,
			  athlon-ieu,
			  athlon-store")

(define_insn_reservation "athlon_idirect_store" 1
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "store"))))
			 "athlon-direct,(athlon-ieu+athlon-agu),
			  athlon-store")
(define_insn_reservation "athlon_idirect_store_amdfam10" 1
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "direct")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "store"))))
			 "athlon-direct,(athlon-ieu+athlon-agu),
			  athlon-store")

(define_insn_reservation "athlon_ivector_store" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "athlon_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "store"))))
			 "athlon-vector,(athlon-ieu+athlon-agu),athlon-ieu,
			  athlon-store")
(define_insn_reservation "athlon_ivector_store_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "amdfam10_decode" "vector")
				   (and (eq_attr "unit" "integer,unknown")
					(eq_attr "memory" "store"))))
			 "athlon-vector,(athlon-ieu+athlon-agu),athlon-ieu,
			  athlon-store")

;; Athlon floatin point unit
(define_insn_reservation "athlon_fldxf" 12
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fmov")
				   (and (eq_attr "memory" "load")
					(eq_attr "mode" "XF"))))
			 "athlon-vector,athlon-fpload2,athlon-fvector*9")
(define_insn_reservation "athlon_fldxf_k8" 13
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fmov")
				   (and (eq_attr "memory" "load")
					(eq_attr "mode" "XF"))))
			 "athlon-vector,athlon-fpload2k8,athlon-fvector*9")
;; Assume superforwarding to take place so effective latency of fany op is 0.
(define_insn_reservation "athlon_fld" 0
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fmov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fany")
(define_insn_reservation "athlon_fld_k8" 2
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fmov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fstore")

(define_insn_reservation "athlon_fstxf" 10
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fmov")
				   (and (eq_attr "memory" "store,both")
					(eq_attr "mode" "XF"))))
			 "athlon-vector,(athlon-fpsched+athlon-agu),(athlon-store2+(athlon-fvector*7))")
(define_insn_reservation "athlon_fstxf_k8" 8
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fmov")
				   (and (eq_attr "memory" "store,both")
					(eq_attr "mode" "XF"))))
			 "athlon-vector,(athlon-fpsched+athlon-agu),(athlon-store2+(athlon-fvector*6))")
(define_insn_reservation "athlon_fst" 4
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fmov")
				   (eq_attr "memory" "store,both")))
			 "athlon-direct,(athlon-fpsched+athlon-agu),(athlon-fstore+athlon-store)")
(define_insn_reservation "athlon_fst_k8" 2
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fmov")
				   (eq_attr "memory" "store,both")))
			 "athlon-direct,(athlon-fpsched+athlon-agu),(athlon-fstore+athlon-store)")
(define_insn_reservation "athlon_fist" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fistp,fisttp"))
			 "athlon-direct,(athlon-fpsched+athlon-agu),(athlon-fstore+athlon-store)")
(define_insn_reservation "athlon_fmov" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fmov"))
			 "athlon-direct,athlon-fpsched,athlon-faddmul")
(define_insn_reservation "athlon_fadd_load" 4
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fop")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fadd")
(define_insn_reservation "athlon_fadd_load_k8" 6
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fop")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_fadd" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fop"))
			 "athlon-direct,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_fmul_load" 4
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fmul")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fmul")
(define_insn_reservation "athlon_fmul_load_k8" 6
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fmul")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fmul")
(define_insn_reservation "athlon_fmul" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fmul"))
			 "athlon-direct,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_fsgn" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fsgn"))
			 "athlon-direct,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_fdiv_load" 24
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fdiv")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fmul")
(define_insn_reservation "athlon_fdiv_load_k8" 13
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fdiv")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fmul")
(define_insn_reservation "athlon_fdiv" 24
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "fdiv"))
			 "athlon-direct,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_fdiv_k8" 11
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (eq_attr "type" "fdiv"))
			 "athlon-direct,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_fpspc_load" 103
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "fpspc")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload,athlon-fvector")
(define_insn_reservation "athlon_fpspc" 100
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fpspc"))
			 "athlon-vector,athlon-fpsched,athlon-fvector")
(define_insn_reservation "athlon_fcmov_load" 7
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fcmov")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload,athlon-fvector")
(define_insn_reservation "athlon_fcmov" 7
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "fcmov"))
			 "athlon-vector,athlon-fpsched,athlon-fvector")
(define_insn_reservation "athlon_fcmov_load_k8" 17
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fcmov")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fploadk8,athlon-fvector")
(define_insn_reservation "athlon_fcmov_k8" 15
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (eq_attr "type" "fcmov"))
			 "athlon-vector,athlon-fpsched,athlon-fvector")
;; fcomi is vector decoded by uses only one pipe.
(define_insn_reservation "athlon_fcomi_load" 3
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fcmp")
				   (and (eq_attr "athlon_decode" "vector")
				        (eq_attr "memory" "load"))))
			 "athlon-vector,athlon-fpload,athlon-fadd")
(define_insn_reservation "athlon_fcomi_load_k8" 5
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fcmp")
				   (and (eq_attr "athlon_decode" "vector")
				        (eq_attr "memory" "load"))))
			 "athlon-vector,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_fcomi" 3
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "athlon_decode" "vector")
				   (eq_attr "type" "fcmp")))
			 "athlon-vector,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_fcom_load" 2
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "fcmp")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fadd")
(define_insn_reservation "athlon_fcom_load_k8" 4
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "fcmp")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_fcom" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (eq_attr "type" "fcmp"))
			 "athlon-direct,athlon-fpsched,athlon-fadd")
;; Never seen by the scheduler because we still don't do post reg-stack
;; scheduling.
;(define_insn_reservation "athlon_fxch" 2
;			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
;			      (eq_attr "type" "fxch"))
;			 "athlon-direct,athlon-fpsched,athlon-fany")

;; Athlon handle MMX operations in the FPU unit with shorter latencies

(define_insn_reservation "athlon_movlpd_load" 0
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssemov")
				   (match_operand:DF 1 "memory_operand" "")))
			 "athlon-direct,athlon-fpload,athlon-fany")
(define_insn_reservation "athlon_movlpd_load_k8" 2
			 (and (eq_attr "cpu" "k8")
			      (and (eq_attr "type" "ssemov")
				   (match_operand:DF 1 "memory_operand" "")))
			 "athlon-direct,athlon-fploadk8,athlon-fstore")
(define_insn_reservation "athlon_movsd_load_generic64" 2
			 (and (eq_attr "cpu" "generic64")
			      (and (eq_attr "type" "ssemov")
				   (match_operand:DF 1 "memory_operand" "")))
			 "athlon-double,athlon-fploadk8,(athlon-fstore+athlon-fmul)")
(define_insn_reservation "athlon_movaps_load_k8" 2
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssemov")
				   (and (eq_attr "mode" "V4SF,V2DF,TI")
					(eq_attr "memory" "load"))))
			 "athlon-double,athlon-fpload2k8,athlon-fstore,athlon-fstore")
(define_insn_reservation "athlon_movaps_load" 0
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssemov")
				   (and (eq_attr "mode" "V4SF,V2DF,TI")
					(eq_attr "memory" "load"))))
			 "athlon-vector,athlon-fpload2,(athlon-fany+athlon-fany)")
(define_insn_reservation "athlon_movss_load" 1
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssemov")
				   (and (eq_attr "mode" "SF,DI")
					(eq_attr "memory" "load"))))
			 "athlon-vector,athlon-fpload,(athlon-fany*2)")
(define_insn_reservation "athlon_movss_load_k8" 1
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssemov")
				   (and (eq_attr "mode" "SF,DI")
					(eq_attr "memory" "load"))))
			 "athlon-double,athlon-fploadk8,(athlon-fstore+athlon-fany)")
(define_insn_reservation "athlon_mmxsseld" 0
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "mmxmov,ssemov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fany")
(define_insn_reservation "athlon_mmxsseld_k8" 2
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "mmxmov,ssemov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fstore")
;; On AMDFAM10 all double, single and integer packed and scalar SSEx data
;; loads  generated are direct path, latency of 2 and do not use any FP
;; executions units. No seperate entries for movlpx/movhpx loads, which
;; are direct path, latency of 4 and use the FADD/FMUL FP execution units,
;; as they will not be generated.
(define_insn_reservation "athlon_sseld_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssemov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8")
;; On AMDFAM10 MMX data loads  generated are direct path, latency of 4
;; and can use any  FP executions units
(define_insn_reservation "athlon_mmxld_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "mmxmov")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8, athlon-fany")			 
(define_insn_reservation "athlon_mmxssest" 3
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "mmxmov,ssemov")
				   (and (eq_attr "mode" "V4SF,V2DF,TI")
					(eq_attr "memory" "store,both"))))
			 "athlon-vector,(athlon-fpsched+athlon-agu),((athlon-fstore+athlon-store2)*2)")
(define_insn_reservation "athlon_mmxssest_k8" 3
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "mmxmov,ssemov")
				   (and (eq_attr "mode" "V4SF,V2DF,TI")
					(eq_attr "memory" "store,both"))))
			 "athlon-double,(athlon-fpsched+athlon-agu),((athlon-fstore+athlon-store2)*2)")
(define_insn_reservation "athlon_mmxssest_short" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "mmxmov,ssemov")
				   (eq_attr "memory" "store,both")))
			 "athlon-direct,(athlon-fpsched+athlon-agu),(athlon-fstore+athlon-store)")
;; On AMDFAM10 all double, single and integer packed SSEx data stores
;; generated are all double path, latency of 2 and use the FSTORE FP
;; execution unit. No entries seperate for movupx/movdqu, which are
;; vector path, latency of 3 and use the FSTORE*2 FP execution unit,
;; as they will not be generated.
(define_insn_reservation "athlon_ssest_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssemov")
				   (and (eq_attr "mode" "V4SF,V2DF,TI")
					(eq_attr "memory" "store,both"))))
			 "athlon-double,(athlon-fpsched+athlon-agu),((athlon-fstore+athlon-store)*2)")
;; On AMDFAM10 all double, single and integer scalar SSEx and MMX
;; data stores generated are all direct path, latency of 2 and use
;; the FSTORE FP execution unit
(define_insn_reservation "athlon_mmxssest_short_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "mmxmov,ssemov")
				   (eq_attr "memory" "store,both")))
			 "athlon-direct,(athlon-fpsched+athlon-agu),(athlon-fstore+athlon-store)")
(define_insn_reservation "athlon_movaps_k8" 2
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssemov")
				   (eq_attr "mode" "V4SF,V2DF,TI")))
			 "athlon-double,athlon-fpsched,((athlon-faddmul+athlon-faddmul) | (athlon-faddmul, athlon-faddmul))")
(define_insn_reservation "athlon_movaps" 2
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssemov")
				   (eq_attr "mode" "V4SF,V2DF,TI")))
			 "athlon-vector,athlon-fpsched,(athlon-faddmul+athlon-faddmul)")
(define_insn_reservation "athlon_mmxssemov" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "type" "mmxmov,ssemov"))
			 "athlon-direct,athlon-fpsched,athlon-faddmul")
(define_insn_reservation "athlon_mmxmul_load" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "mmxmul")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-fmul")
(define_insn_reservation "athlon_mmxmul" 3
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "type" "mmxmul"))
			 "athlon-direct,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_mmx_load" 3
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "unit" "mmx")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fpload,athlon-faddmul")
(define_insn_reservation "athlon_mmx" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "unit" "mmx"))
			 "athlon-direct,athlon-fpsched,athlon-faddmul")
;; SSE operations are handled by the i387 unit as well.  The latency
;; is same as for i387 operations for scalar operations

(define_insn_reservation "athlon_sselog_load" 3
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "sselog,sselog1")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload2,(athlon-fmul*2)")
(define_insn_reservation "athlon_sselog_load_k8" 5
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "sselog,sselog1")
				   (eq_attr "memory" "load")))
			 "athlon-double,athlon-fpload2k8,(athlon-fmul*2)")
(define_insn_reservation "athlon_sselog_load_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sselog,sselog1")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,(athlon-fadd|athlon-fmul)")
(define_insn_reservation "athlon_sselog" 3
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "sselog,sselog1"))
			 "athlon-vector,athlon-fpsched,athlon-fmul*2")
(define_insn_reservation "athlon_sselog_k8" 3
			 (and (eq_attr "cpu" "k8,generic64")
			      (eq_attr "type" "sselog,sselog1"))
			 "athlon-double,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_sselog_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "sselog,sselog1"))
			 "athlon-direct,athlon-fpsched,(athlon-fadd|athlon-fmul)")

;; ??? pcmp executes in addmul, probably not worthwhile to bother about that.
(define_insn_reservation "athlon_ssecmp_load" 2
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssecmp")
				   (and (eq_attr "mode" "SF,DF,DI")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fpload,athlon-fadd")
(define_insn_reservation "athlon_ssecmp_load_k8" 4
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "ssecmp")
				   (and (eq_attr "mode" "SF,DF,DI,TI")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_ssecmp" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "ssecmp")
				   (eq_attr "mode" "SF,DF,DI,TI")))
			 "athlon-direct,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_ssecmpvector_load" 3
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssecmp")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload2,(athlon-fadd*2)")
(define_insn_reservation "athlon_ssecmpvector_load_k8" 5
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssecmp")
				   (eq_attr "memory" "load")))
			 "athlon-double,athlon-fpload2k8,(athlon-fadd*2)")
(define_insn_reservation "athlon_ssecmpvector_load_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecmp")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_ssecmpvector" 3
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "ssecmp"))
			 "athlon-vector,athlon-fpsched,(athlon-fadd*2)")
(define_insn_reservation "athlon_ssecmpvector_k8" 3
			 (and (eq_attr "cpu" "k8,generic64")
			      (eq_attr "type" "ssecmp"))
			 "athlon-double,athlon-fpsched,(athlon-fadd*2)")
(define_insn_reservation "athlon_ssecmpvector_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "ssecmp"))
			 "athlon-direct,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_ssecomi_load" 4
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssecomi")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload,athlon-fadd")
(define_insn_reservation "athlon_ssecomi_load_k8" 6
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssecomi")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_ssecomi_load_amdfam10" 5
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecomi")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_ssecomi" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (eq_attr "type" "ssecmp"))
			 "athlon-vector,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_ssecomi_amdfam10" 3
			 (and (eq_attr "cpu" "amdfam10")
;; It seems athlon_ssecomi has a bug in the attr_type, fixed for amdfam10
			      (eq_attr "type" "ssecomi"))
			 "athlon-direct,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_sseadd_load" 4
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "sseadd")
				   (and (eq_attr "mode" "SF,DF,DI")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fpload,athlon-fadd")
(define_insn_reservation "athlon_sseadd_load_k8" 6
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "sseadd")
				   (and (eq_attr "mode" "SF,DF,DI")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_sseadd" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "sseadd")
				   (eq_attr "mode" "SF,DF,DI")))
			 "athlon-direct,athlon-fpsched,athlon-fadd")
(define_insn_reservation "athlon_sseaddvector_load" 5
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "sseadd")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload2,(athlon-fadd*2)")
(define_insn_reservation "athlon_sseaddvector_load_k8" 7
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "sseadd")
				   (eq_attr "memory" "load")))
			 "athlon-double,athlon-fpload2k8,(athlon-fadd*2)")
(define_insn_reservation "athlon_sseaddvector_load_amdfam10" 6
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseadd")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fadd")
(define_insn_reservation "athlon_sseaddvector" 5
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "sseadd"))
			 "athlon-vector,athlon-fpsched,(athlon-fadd*2)")
(define_insn_reservation "athlon_sseaddvector_k8" 5
			 (and (eq_attr "cpu" "k8,generic64")
			      (eq_attr "type" "sseadd"))
			 "athlon-double,athlon-fpsched,(athlon-fadd*2)")
(define_insn_reservation "athlon_sseaddvector_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "sseadd"))
			 "athlon-direct,athlon-fpsched,athlon-fadd")

;; Conversions behaves very irregularly and the scheduling is critical here.
;; Take each instruction separately.  Assume that the mode is always set to the
;; destination one and athlon_decode is set to the K8 versions.

;; cvtss2sd
(define_insn_reservation "athlon_ssecvt_cvtss2sd_load_k8" 4
			 (and (eq_attr "cpu" "k8,athlon,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "direct")
					(and (eq_attr "mode" "DF")
					     (eq_attr "memory" "load")))))
			 "athlon-direct,athlon-fploadk8,athlon-fstore")
(define_insn_reservation "athlon_ssecvt_cvtss2sd_load_amdfam10" 7
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "DF")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
(define_insn_reservation "athlon_ssecvt_cvtss2sd" 2
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "direct")
					(eq_attr "mode" "DF"))))
			 "athlon-direct,athlon-fpsched,athlon-fstore")
(define_insn_reservation "athlon_ssecvt_cvtss2sd_amdfam10" 7
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "vector")
					(eq_attr "mode" "DF"))))
			 "athlon-vector,athlon-fpsched,athlon-faddmul,(athlon-fstore*2)")
;; cvtps2pd.  Model same way the other double decoded FP conversions.
(define_insn_reservation "athlon_ssecvt_cvtps2pd_load_k8" 5
			 (and (eq_attr "cpu" "k8,athlon,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "V2DF,V4SF,TI")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fpload2k8,(athlon-fstore*2)")
(define_insn_reservation "athlon_ssecvt_cvtps2pd_load_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "direct")
					(and (eq_attr "mode" "V2DF,V4SF,TI")
					     (eq_attr "memory" "load")))))
			 "athlon-direct,athlon-fploadk8,athlon-fstore")
(define_insn_reservation "athlon_ssecvt_cvtps2pd_k8" 3
			 (and (eq_attr "cpu" "k8,athlon,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "double")
					(eq_attr "mode" "V2DF,V4SF,TI"))))
			 "athlon-double,athlon-fpsched,athlon-fstore,athlon-fstore")
(define_insn_reservation "athlon_ssecvt_cvtps2pd_amdfam10" 2
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "direct")
					(eq_attr "mode" "V2DF,V4SF,TI"))))
			 "athlon-direct,athlon-fpsched,athlon-fstore")
;; cvtsi2sd mem,reg is directpath path  (cvtsi2sd reg,reg is doublepath)
;; cvtsi2sd has troughput 1 and is executed in store unit with latency of 6
(define_insn_reservation "athlon_sseicvt_cvtsi2sd_load" 6
			 (and (eq_attr "cpu" "athlon,k8")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "direct")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "load")))))
			 "athlon-direct,athlon-fploadk8,athlon-fstore")
(define_insn_reservation "athlon_sseicvt_cvtsi2sd_load_amdfam10" 9
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
;; cvtsi2ss mem, reg is doublepath
(define_insn_reservation "athlon_sseicvt_cvtsi2ss_load" 9
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "load")))))
			 "athlon-vector,athlon-fpload,(athlon-fstore*2)")
(define_insn_reservation "athlon_sseicvt_cvtsi2ss_load_k8" 9
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-fstore*2)")
(define_insn_reservation "athlon_sseicvt_cvtsi2ss_load_amdfam10" 9
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")			 
;; cvtsi2sd reg,reg is double decoded (vector on Athlon)
(define_insn_reservation "athlon_sseicvt_cvtsi2sd_k8" 11
			 (and (eq_attr "cpu" "k8,athlon,generic64")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "none")))))
			 "athlon-double,athlon-fploadk8,athlon-fstore")
(define_insn_reservation "athlon_sseicvt_cvtsi2sd_amdfam10" 14
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "vector")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
;; cvtsi2ss reg, reg is doublepath
(define_insn_reservation "athlon_sseicvt_cvtsi2ss" 14
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "vector")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fploadk8,(athlon-fvector*2)")
(define_insn_reservation "athlon_sseicvt_cvtsi2ss_amdfam10" 14
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "vector")
					(and (eq_attr "mode" "SF,DF")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
;; cvtsd2ss mem,reg is doublepath, troughput unknown, latency 9
(define_insn_reservation "athlon_ssecvt_cvtsd2ss_load_k8" 9
			 (and (eq_attr "cpu" "k8,athlon,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "SF")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-fstore*3)")
(define_insn_reservation "athlon_ssecvt_cvtsd2ss_load_amdfam10" 9
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "SF")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
;; cvtsd2ss reg,reg is vectorpath, troughput unknown, latency 12
(define_insn_reservation "athlon_ssecvt_cvtsd2ss" 12
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "vector")
					(and (eq_attr "mode" "SF")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fpsched,(athlon-fvector*3)")
(define_insn_reservation "athlon_ssecvt_cvtsd2ss_amdfam10" 8
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "vector")
					(and (eq_attr "mode" "SF")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fpsched,athlon-faddmul,(athlon-fstore*2)")
(define_insn_reservation "athlon_ssecvt_cvtpd2ps_load_k8" 8
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "vector")
					(and (eq_attr "mode" "V4SF,V2DF,TI")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fpload2k8,(athlon-fstore*3)")
(define_insn_reservation "athlon_ssecvt_cvtpd2ps_load_amdfam10" 9
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "V4SF,V2DF,TI")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
;; cvtpd2ps mem,reg is vectorpath, troughput unknown, latency 10
;; ??? Why it is fater than cvtsd2ss?
(define_insn_reservation "athlon_ssecvt_cvtpd2ps" 8
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "athlon_decode" "vector")
					(and (eq_attr "mode" "V4SF,V2DF,TI")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fpsched,athlon-fvector*2")
(define_insn_reservation "athlon_ssecvt_cvtpd2ps_amdfam10" 7
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssecvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "V4SF,V2DF,TI")
					     (eq_attr "memory" "none")))))
			 "athlon-double,athlon-fpsched,(athlon-faddmul+athlon-fstore)")
;; cvtsd2si mem,reg is doublepath, troughput 1, latency 9
(define_insn_reservation "athlon_secvt_cvtsX2si_load" 9
			 (and (eq_attr "cpu" "athlon,k8,generic64")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "vector")
					(and (eq_attr "mode" "SI,DI")
					     (eq_attr "memory" "load")))))
			 "athlon-vector,athlon-fploadk8,athlon-fvector")
(define_insn_reservation "athlon_secvt_cvtsX2si_load_amdfam10" 10
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "SI,DI")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-fadd+athlon-fstore)")
;; cvtsd2si reg,reg is doublepath, troughput 1, latency 9
(define_insn_reservation "athlon_ssecvt_cvtsX2si" 9
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "SI,DI")
					     (eq_attr "memory" "none")))))
			 "athlon-vector,athlon-fpsched,athlon-fvector")
(define_insn_reservation "athlon_ssecvt_cvtsX2si_k8" 9
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "athlon_decode" "double")
					(and (eq_attr "mode" "SI,DI")
					     (eq_attr "memory" "none")))))
			 "athlon-double,athlon-fpsched,athlon-fstore")
(define_insn_reservation "athlon_ssecvt_cvtsX2si_amdfam10" 8
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "SI,DI")
					     (eq_attr "memory" "none")))))
			 "athlon-double,athlon-fpsched,(athlon-fadd+athlon-fstore)")
;; cvtpd2dq reg,mem is doublepath, troughput 1, latency 9 on amdfam10
(define_insn_reservation "athlon_sseicvt_cvtpd2dq_load_amdfam10" 9
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "TI")
					     (eq_attr "memory" "load")))))
			 "athlon-double,athlon-fploadk8,(athlon-faddmul+athlon-fstore)")
;; cvtpd2dq reg,mem is doublepath, troughput 1, latency 7 on amdfam10
(define_insn_reservation "athlon_sseicvt_cvtpd2dq_amdfam10" 7
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "sseicvt")
				   (and (eq_attr "amdfam10_decode" "double")
					(and (eq_attr "mode" "TI")
					     (eq_attr "memory" "none")))))
			 "athlon-double,athlon-fpsched,(athlon-faddmul+athlon-fstore)")


(define_insn_reservation "athlon_ssemul_load" 4
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssemul")
				   (and (eq_attr "mode" "SF,DF")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fpload,athlon-fmul")
(define_insn_reservation "athlon_ssemul_load_k8" 6
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "ssemul")
				   (and (eq_attr "mode" "SF,DF")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fploadk8,athlon-fmul")
(define_insn_reservation "athlon_ssemul" 4
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "ssemul")
				   (eq_attr "mode" "SF,DF")))
			 "athlon-direct,athlon-fpsched,athlon-fmul")
(define_insn_reservation "athlon_ssemulvector_load" 5
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssemul")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload2,(athlon-fmul*2)")
(define_insn_reservation "athlon_ssemulvector_load_k8" 7
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssemul")
				   (eq_attr "memory" "load")))
			 "athlon-double,athlon-fpload2k8,(athlon-fmul*2)")
(define_insn_reservation "athlon_ssemulvector_load_amdfam10" 6
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssemul")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fmul")
(define_insn_reservation "athlon_ssemulvector" 5
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "ssemul"))
			 "athlon-vector,athlon-fpsched,(athlon-fmul*2)")
(define_insn_reservation "athlon_ssemulvector_k8" 5
			 (and (eq_attr "cpu" "k8,generic64")
			      (eq_attr "type" "ssemul"))
			 "athlon-double,athlon-fpsched,(athlon-fmul*2)")
(define_insn_reservation "athlon_ssemulvector_amdfam10" 4
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "ssemul"))
			 "athlon-direct,athlon-fpsched,athlon-fmul")			 
;; divsd timings.  divss is faster
(define_insn_reservation "athlon_ssediv_load" 20
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssediv")
				   (and (eq_attr "mode" "SF,DF")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fpload,athlon-fmul*17")
(define_insn_reservation "athlon_ssediv_load_k8" 22
			 (and (eq_attr "cpu" "k8,generic64,amdfam10")
			      (and (eq_attr "type" "ssediv")
				   (and (eq_attr "mode" "SF,DF")
					(eq_attr "memory" "load"))))
			 "athlon-direct,athlon-fploadk8,athlon-fmul*17")
(define_insn_reservation "athlon_ssediv" 20
			 (and (eq_attr "cpu" "athlon,k8,generic64,amdfam10")
			      (and (eq_attr "type" "ssediv")
				   (eq_attr "mode" "SF,DF")))
			 "athlon-direct,athlon-fpsched,athlon-fmul*17")
(define_insn_reservation "athlon_ssedivvector_load" 39
			 (and (eq_attr "cpu" "athlon")
			      (and (eq_attr "type" "ssediv")
				   (eq_attr "memory" "load")))
			 "athlon-vector,athlon-fpload2,athlon-fmul*34")
(define_insn_reservation "athlon_ssedivvector_load_k8" 35
			 (and (eq_attr "cpu" "k8,generic64")
			      (and (eq_attr "type" "ssediv")
				   (eq_attr "memory" "load")))
			 "athlon-double,athlon-fpload2k8,athlon-fmul*34")
(define_insn_reservation "athlon_ssedivvector_load_amdfam10" 22
			 (and (eq_attr "cpu" "amdfam10")
			      (and (eq_attr "type" "ssediv")
				   (eq_attr "memory" "load")))
			 "athlon-direct,athlon-fploadk8,athlon-fmul*17")			 
(define_insn_reservation "athlon_ssedivvector" 39
			 (and (eq_attr "cpu" "athlon")
			      (eq_attr "type" "ssediv"))
			 "athlon-vector,athlon-fmul*34")
(define_insn_reservation "athlon_ssedivvector_k8" 39
			 (and (eq_attr "cpu" "k8,generic64")
			      (eq_attr "type" "ssediv"))
			 "athlon-double,athlon-fmul*34")
(define_insn_reservation "athlon_ssedivvector_amdfam10" 20
			 (and (eq_attr "cpu" "amdfam10")
			      (eq_attr "type" "ssediv"))
			 "athlon-direct,athlon-fmul*17")
(define_insn_reservation "athlon_sseins_amdfam10" 5
                         (and (eq_attr "cpu" "amdfam10")
                              (and (eq_attr "type" "sseins")
                                   (eq_attr "mode" "TI")))
                         "athlon-vector,athlon-fpsched,athlon-faddmul")
