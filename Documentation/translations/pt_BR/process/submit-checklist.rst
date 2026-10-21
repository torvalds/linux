.. SPDX-License-Identifier: GPL-2.0

==============================================================
Lista de verificação para submissão de patches do kernel Linux
==============================================================

Aqui estão algumas coisas básicas que os desenvolvedores devem fazer se
quiserem ver suas submissões de patches de kernel aceitas mais rapidamente.

Estas diretrizes vão além da documentação fornecida em
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>`
e em outros locais sobre o envio de patches para o kernel Linux.

Revise seu código
=================

1) Se você usar um recurso, faça o #include do arquivo que define/declara
   esse recurso. Não dependa de outros arquivos de cabeçalho que incluam
   os que você usa de forma indireta.

2) Verifique o estilo geral do seu patch conforme detalhado em
   :ref:`Documentation//process/coding-style.rst <codingstyle>`.

3) Todas as barreiras de memória {por exemplo, ``barrier()``, ``rmb()``,
   ``wmb()``} precisam de um comentário no código-fonte que explique a
   lógica do que estão fazendo e o porquê.

Revise as alterações do Kconfig
===============================

1) Quaisquer novas ou modificadas opções de ``CONFIG`` não bagunçam o
   menu de configuração e têm 'desativado' (off) como padrão, a menos que
   atendam aos critérios de exceção documentados em
   ``Documentation/kbuild/kconfig-language.rst``, atributos de menu: valor
   padrão.

2) Todas as novas opções de ``Kconfig`` possuem texto de ajuda.

3) Foram cuidadosamente revisadas com relação às combinações relevantes de
   ``Kconfig``. Isso é muito difícil de acertar apenas com testes --- exige
   capacidade de raciocínio.
   pays off here.

Forneça documentação
====================

1) Inclua :ref:`kernel-doc <kernel_doc>` para documentar as APIs globais
   do kernel. (Não é obrigatório para funções estáticas, mas também é
   aceitável nelas.)

2) Todas as novas entradas em ``/proc`` devem ser documentadas sob
   ``Documentation/``.

3) Todos os novos parâmetros de inicialização (boot) do kernel devem ser
   documentados em ``Documentation/admin-guide/kernel-parameters.rst``.

4) Todos os novos parâmetros de módulo devem ser documentados com
   ``MODULE_PARM_DESC()``.

5) Todas as novas interfaces com o espaço de usuário (userspace) devem ser
   documentadas em ``Documentation/ABI/``. Consulte
   ``Documentation/admin-guide/abi.rst`` (ou ``Documentation/ABI/README``)
   para obter mais informações. Patches que alteram interfaces de espaço
   de usuário devem incluir em cópia (CC) linux-api@vger.kernel.org.

6) Se quaisquer ioctls forem adicionados pelo patch, atualize também
   ``Documentation/userspace-api/ioctl/ioctl-number.rst``.

Verifique seu código com ferramentas
====================================

1) Verifique se há violações triviais com o verificador de estilo de patch
   antes do envio (``scripts/checkpatch.pl``). Você deve ser capaz de
   justificar todas as violações que permanecerem no seu patch.

2) Faça uma verificação limpa com o sparse.

3) Use ``make checkstack`` e corrija quaisquer problemas encontrados por ele.
   Observe que o ``checkstack`` não aponta problemas explicitamente, mas
   qualquer função individual que utilize mais de 512 bytes na pilha é uma
   candidata a alteração.

Compile seu código
==================

1) Compila de forma limpa:

  a) com as opções de ``CONFIG`` aplicáveis ou modificadas definidas como
     ``=y``, ``=m`` e ``=n``. Sem avisos/erros do ``gcc``, sem avisos/erros do
     vinculador (linker).

  b) Passa em ``allnoconfig``, ``allmodconfig``

  c) Compila com sucesso ao usar ``O=builddir``

  d) Quaisquer alterações em Documentation/ compilam com sucesso sem novos
     avisos/erros. Use ``make htmldocs`` ou ``make pdfdocs`` para verificar
     a compilação e corrigir quaisquer problemas.

2) Compila em múltiplas arquiteturas de CPU usando ferramentas locais de
   compilação cruzada (cross-compile) ou alguma outra fazenda de compilação
   (build farm).
   Observe que testar em arquiteturas de diferentes tamanhos de palavra
   (32 e 64 bits) e diferentes endianness (big- e little-endian) é eficaz
   para capturar vários problemas de portabilidade decorrentes de falsas
   suposições sobre o intervalo de quantidade representável, alinhamento
   de dados ou endianness, entre outros.

3) O novo código adicionado foi compilado com ``gcc -W`` (use
   ``make KCFLAGS=-W``). Isso gerará muito ruído, mas é bom para encontrar
   bugs como "warning: comparison between signed and unsigned".

4) Se o seu código-fonte modificado depender ou usar quaisquer APIs ou
   recursos do kernel relacionados aos seguintes símbolos do ``Kconfig``,
   teste múltiplas compilações com os símbolos relacionados do ``Kconfig``
   desativados e/ou definidos como ``=m`` (se essa opção estiver disponível)
   [não todos ao mesmo tempo, apenas combinações variadas/aleatórias deles]:

   ``CONFIG_SMP``, ``CONFIG_SYSFS``, ``CONFIG_PROC_FS``, ``CONFIG_INPUT``,
   ``CONFIG_PCI``, ``CONFIG_BLOCK``, ``CONFIG_PM``, ``CONFIG_MAGIC_SYSRQ``,
   ``CONFIG_NET``, ``CONFIG_INET=n`` (mas este último com ``CONFIG_NET=y``).

Teste seu código
================

1) Foi testado com ``CONFIG_PREEMPT``, ``CONFIG_DEBUG_PREEMPT``,
   ``CONFIG_SLUB_DEBUG``, ``CONFIG_DEBUG_PAGEALLOC``,
   ``CONFIG_DEBUG_MUTEXES``, ``CONFIG_DEBUG_SPINLOCK``,
   ``CONFIG_DEBUG_ATOMIC_SLEEP``, ``CONFIG_PROVE_RCU`` e
   ``CONFIG_DEBUG_OBJECTS_RCU_HEAD`` todos habilitados simultaneamente.

2) Foi testado em tempo de compilação e de execução com e sem ``CONFIG_SMP``
   e ``CONFIG_PREEMPT``.

3) Todos os caminhos de código foram executados com todos os recursos de
   lockdep ativados.

4) Foi verificado com a injeção de falhas de pelo menos slab e alocação de
   páginas. Consulte ``Documentation/fault-injection/``.
   Se o novo código for substancial, a adição de injeção de falhas específica
   do subsistema pode ser apropriada.

5) Testado com a tag mais recente do linux-next para garantir que ele ainda
   funcione com todos os outros patches enfileirados e com várias alterações
   na VM, VFS e outros subsistemas.
