.. SPDX-License-Identifier: GPL-2.0

====================================================
Conselhos de depuração a partir do espaço de usuário
====================================================

Este documento fornece uma breve visão geral das ferramentas mais comuns
para depurar o kernel Linux a partir do espaço de usuário.
Para conselhos de depuração voltados a desenvolvedores de drivers, veja
:doc:`aqui </process/debugging/driver_development_debugging_guide>`.
Para conselhos gerais de depuração, veja o :doc:`documento de conselhos
gerais <index>`.

.. contents::
    :depth: 3

As seções a seguir apresentam as ferramentas disponíveis.

Dynamic debug
-------------

Mecanismo para filtrar o que chega ao log do kernel, desabilitando ou
habilitando mensagens de log.

Pré-requisito: ``CONFIG_DYNAMIC_DEBUG``

O dynamic debug só consegue atuar sobre:

- pr_debug()
- dev_dbg()
- print_hex_dump_debug()
- print_hex_dump_bytes()

Por isso a utilidade dessa ferramenta é, até o momento, bastante limitada,
já que não há uma regra uniforme para adicionar prints de depuração ao
código-fonte, o que resulta em uma variedade de formas de implementar
esses prints.

Note também que a maioria das instruções de depuração é implementada como
uma variação de dprintk(), que precisa ser ativada por um parâmetro no
módulo correspondente; o dynamic debug não é capaz de fazer essa etapa por
você.

Eis um exemplo que habilita todos os pr_debug() disponíveis no arquivo::

  $ alias ddcmd='echo $* > /proc/dynamic_debug/control'
  $ ddcmd '-p; file v4l2-h264.c +p'
  $ grep =p /proc/dynamic_debug/control
   drivers/media/v4l2-core/v4l2-h264.c:372 [v4l2_h264]print_ref_list_b =p
   "ref_pic_list_b%u (cur_poc %u%c) %s"
   drivers/media/v4l2-core/v4l2-h264.c:333 [v4l2_h264]print_ref_list_p =p
   "ref_pic_list_p (cur_poc %u%c) %s\n"

**Quando usar isto em vez do Ftrace?**

- Quando o código contém uma das instruções de print válidas (veja acima)
  ou quando você adicionou várias instruções pr_debug() durante o
  desenvolvimento
- Quando o tempo não é um problema, ou seja, se várias instruções
  pr_debug() no código não causarem atrasos
- Quando você se importa mais em receber mensagens de log específicas do
  que em rastrear o padrão de chamadas de uma função

Para a documentação completa, veja
:doc:`/admin-guide/dynamic-debug-howto`

Ftrace
------

Pré-requisito: ``CONFIG_DYNAMIC_FTRACE``

Esta ferramenta usa o sistema de arquivos tracefs para os arquivos de
controle e de saída. Esse sistema de arquivos é montado como um diretório
``tracing``, que pode ser encontrado em ``/sys/kernel/`` ou em
``/sys/debug/kernel/``.

Algumas das operações mais importantes para depuração são:

- Você pode fazer um rastreamento de função adicionando o nome da função
  ao arquivo ``set_ftrace_filter`` (que aceita qualquer nome de função
  encontrado no arquivo ``available_filter_functions``) ou pode
  desabilitar funções específicas adicionando seus nomes ao arquivo
  ``set_ftrace_notrace`` (mais informações em:
  :ref:`trace/ftrace:dynamic ftrace`).
- Para descobrir de onde partem as chamadas, você pode ativar a opção
  ``func_stack_trace`` em ``options/func_stack_trace``.
- Rastrear as funções filhas de uma chamada e exibir os valores de retorno
  é possível adicionando a função desejada ao arquivo
  ``set_graph_function`` (requer a configuração ``FUNCTION_GRAPH_RETVAL``);
  mais informações em
  :ref:`trace/ftrace:dynamic ftrace with the function graph tracer`.

Para a documentação completa do Ftrace, veja :doc:`/trace/ftrace`

Você também pode rastrear eventos específicos :ref:`usando o rastreamento
de eventos <trace/events:2. using event tracing>`, que pode ser definido
conforme descrito aqui: :ref:`Criando um tracepoint customizado do Ftrace
<process/debugging/driver_development_debugging_guide:ftrace>`.

Para a documentação completa do rastreamento de eventos do Ftrace, veja
:doc:`/trace/events`

.. _pt_BR_read_ftrace_log:

Lendo o log do ftrace
~~~~~~~~~~~~~~~~~~~~~

O arquivo ``trace`` pode ser lido como qualquer outro arquivo (``cat``,
``tail``, ``head``, ``vim``, etc.); o tamanho do arquivo é limitado pelo
``buffer_size_kb`` (``echo 1000 > buffer_size_kb``). O
:ref:`trace/ftrace:trace_pipe` se comporta de forma semelhante ao arquivo
``trace``, mas sempre que você lê o arquivo o conteúdo é consumido.

Kernelshark
~~~~~~~~~~~

Uma interface gráfica para visualizar os rastreamentos em forma de gráfico
e de lista, a partir da saída da aplicação `trace-cmd
<https://git.kernel.org/pub/scm/utils/trace-cmd/trace-cmd.git/>`__.

Para a documentação completa, veja
`<https://kernelshark.org/Documentation.html>`__

Perf e alternativas
-------------------

As ferramentas mencionadas acima oferecem maneiras de inspecionar o código
do kernel, resultados, valores de variáveis, etc. Às vezes você precisa
primeiro descobrir onde olhar e, para esses casos, um conjunto de
ferramentas de acompanhamento de desempenho pode ajudar a delimitar o
problema.

Por que fazer uma análise de desempenho?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Uma análise de desempenho é um bom primeiro passo quando, entre outros
motivos:

- você não consegue definir o problema
- você não sabe onde ele ocorre
- o sistema em execução não deve ser interrompido, ou é um sistema remoto
  no qual você não pode instalar um novo módulo/kernel

Como fazer uma análise simples com ferramentas do Linux?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Para iniciar uma análise de desempenho, você pode começar com as
ferramentas usuais, como:

- ``top`` / ``htop`` / ``atop`` (*obtenha uma visão geral da carga do
  sistema, veja picos em processos específicos*)
- ``mpstat -P ALL`` (*observe a distribuição de carga entre as CPUs*)
- ``iostat -x`` (*observe a utilização e o desempenho dos dispositivos de
  entrada e saída*)
- ``vmstat`` (*visão geral do uso de memória no sistema*)
- ``pidstat`` (*semelhante ao* ``vmstat`` *, mas por processo, para
  restringir ao alvo*)
- ``strace -tp $PID`` (*uma vez que você conhece o processo, pode descobrir
  como ele se comunica com o kernel*)

Isso deve ajudar a reduzir suficientemente as áreas a serem investigadas.

Indo mais fundo com o perf
~~~~~~~~~~~~~~~~~~~~~~~~~~

A ferramenta **perf** fornece uma série de métricas e eventos para
aprofundar a investigação dos problemas.

Pré-requisito: compilar ou instalar o perf no seu sistema

Coletar dados estatísticos ao procurar todos os arquivos que começam com
``gcc`` em ``/usr``::

  # perf stat -d find /usr -name 'gcc*' | wc -l

   Performance counter stats for 'find /usr -name gcc*':

     1277.81 msec    task-clock             #    0.997 CPUs utilized
     9               context-switches       #    7.043 /sec
     1               cpu-migrations         #    0.783 /sec
     704             page-faults            #  550.943 /sec
     766548897       cycles                 #    0.600 GHz                         (97.15%)
     798285467       instructions           #    1.04  insn per cycle              (97.15%)
     57582731        branches               #   45.064 M/sec                       (2.85%)
     3842573         branch-misses          #    6.67% of all branches             (97.15%)
     281616097       L1-dcache-loads        #  220.390 M/sec                       (97.15%)
     4220975         L1-dcache-load-misses  #    1.50% of all L1-dcache accesses   (97.15%)
     <not supported> LLC-loads
     <not supported> LLC-load-misses

   1.281746009 seconds time elapsed

   0.508796000 seconds user
   0.773209000 seconds sys


  52

A disponibilidade de eventos e métricas depende do sistema em que você está
executando.

Para a documentação completa, veja
`<https://perf.wiki.kernel.org/index.php/Main_Page>`__

Perfetto
~~~~~~~~

Um conjunto de ferramentas para medir e analisar o desempenho de
aplicações e sistemas. Você pode usá-lo para:

* identificar gargalos
* otimizar código
* fazer o software rodar mais rápido e de forma mais eficiente.

**Qual é a diferença entre o perfetto e o perf?**

* o perf é uma ferramenta que faz parte do kernel Linux e é especializada
  nele, com interface de linha de comando.
* o Perfetto é um conjunto multiplataforma de ferramentas para análise de
  desempenho, estende suas funcionalidades ao espaço de usuário e oferece
  uma interface web.

Para a documentação completa, veja `<https://perfetto.dev/docs/>`__

Ferramentas de análise de kernel panic
--------------------------------------

  Para capturar o crash dump, use ``Kdump`` e ``Kexec``. Abaixo você
  encontra alguns conselhos para analisar os dados.

  Para a documentação completa, veja :doc:`/admin-guide/kdump/kdump`

  Para encontrar a linha correspondente no código, você pode usar o
  `faddr2line
  <https://elixir.bootlin.com/linux/v6.11.6/source/scripts/faddr2line>`__;
  note que é preciso habilitar ``CONFIG_DEBUG_INFO`` para que isso
  funcione.

  Uma alternativa ao ``faddr2line`` é o uso do ``objdump`` (e de suas
  variantes para as diferentes plataformas, como
  ``aarch64-linux-gnu-objdump``). Tome esta linha como exemplo:

  ``[  +0.000240]  rkvdec_device_run+0x50/0x138 [rockchip_vdec]``.

  Podemos encontrar a linha de código correspondente executando::

    aarch64-linux-gnu-objdump -dS drivers/staging/media/rkvdec/rockchip-vdec.ko | grep rkvdec_device_run\>: -A 40
    0000000000000ac8 <rkvdec_device_run>:
     ac8:	d503201f 	nop
     acc:	d503201f 	nop
    {
     ad0:	d503233f 	paciasp
     ad4:	a9bd7bfd 	stp	x29, x30, [sp, #-48]!
     ad8:	910003fd 	mov	x29, sp
     adc:	a90153f3 	stp	x19, x20, [sp, #16]
     ae0:	a9025bf5 	stp	x21, x22, [sp, #32]
        const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
     ae4:	f9411814 	ldr	x20, [x0, #560]
        struct rkvdec_dev *rkvdec = ctx->dev;
     ae8:	f9418015 	ldr	x21, [x0, #768]
        if (WARN_ON(!desc))
     aec:	b4000654 	cbz	x20, bb4 <rkvdec_device_run+0xec>
        ret = pm_runtime_resume_and_get(rkvdec->dev);
     af0:	f943d2b6 	ldr	x22, [x21, #1952]
        ret = __pm_runtime_resume(dev, RPM_GET_PUT);
     af4:	aa0003f3 	mov	x19, x0
     af8:	52800081 	mov	w1, #0x4                   	// #4
     afc:	aa1603e0 	mov	x0, x22
     b00:	94000000 	bl	0 <__pm_runtime_resume>
        if (ret < 0) {
     b04:	37f80340 	tbnz	w0, #31, b6c <rkvdec_device_run+0xa4>
        dev_warn(rkvdec->dev, "Not good\n");
     b08:	f943d2a0 	ldr	x0, [x21, #1952]
     b0c:	90000001 	adrp	x1, 0 <rkvdec_try_ctrl-0x8>
     b10:	91000021 	add	x1, x1, #0x0
     b14:	94000000 	bl	0 <_dev_warn>
        *bad = 1;
     b18:	d2800001 	mov	x1, #0x0                   	// #0
     ...

  Ou seja, nesta linha do crash dump::

    [  +0.000240]  rkvdec_device_run+0x50/0x138 [rockchip_vdec]

  Posso tomar ``0x50`` como deslocamento, que devo somar ao endereço base
  da função correspondente, que encontro nesta linha::

    0000000000000ac8 <rkvdec_device_run>:

  O resultado de ``0xac8 + 0x50 = 0xb18``
  E quando procuro por esse endereço dentro da função, obtenho a seguinte
  linha::

    *bad = 1;
    b18:      d2800001        mov     x1, #0x0

**Copyright** ©2024 : Collabora
