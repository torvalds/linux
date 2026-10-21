.. SPDX-License-Identifier: GPL-2.0

O manual da árvore tip
======================

O que é a árvore tip?
---------------------

A árvore tip é uma coleção de vários subsistemas e áreas de
desenvolvimento. A árvore tip é tanto uma árvore de desenvolvimento direto quanto uma
árvore de agregação para várias árvores de sub-mantenedores. A URL gitweb da árvore tip
é: https://git.kernel.org/pub/scm/linux/kernel/git/tip/tip.git

A árvore tip contém os seguintes subsistemas:

   - **Arquitetura x86**

     O desenvolvimento da arquitetura x86 ocorre na árvore tip, exceto
     pelas partes específicas do KVM e XEN no x86, que são mantidas nos
     subsistemas correspondentes e roteadas diretamente para a mainline a partir
     dali. Ainda é uma boa prática enviar Cc para os mantenedores x86 nos
     patches do KVM e XEN específicos para x86.

     Alguns subsistemas x86 têm seus próprios mantenedores além dos
     mantenedores gerais do x86. Por favor, envie Cc para os mantenedores gerais do x86 em
     patches que toquem em arquivos em arch/x86, mesmo quando não forem indicados
     pelo arquivo MAINTAINER.

     Note que ``x86@kernel.org`` não é uma lista de discussão. É meramente um
     alias de e-mail que distribui mensagens para a equipe de mantenedores de nível superior
     do x86. Por favor, sempre envie Cc para a lista de discussão do Linux Kernel (LKML)
     ``linux-kernel@vger.kernel.org``, caso contrário, seu e-mail acabará apenas nas
     caixas de entrada privadas dos mantenedores.

   - **Scheduler**

     O desenvolvimento do scheduler ocorre na árvore -tip, na
     branch sched/core - com ocasionais árvores de subtópicos para
     conjuntos de patches em progresso.

   - **Locking e atomics**

     O desenvolvimento de locking (incluindo atomics e outras primitivas de
     sincronização que estão conectadas ao locking) ocorre na árvore -tip,
     na branch locking/core - com ocasionais árvores de subtópicos
     para conjuntos de patches em progresso.

   - **Subsistema genérico de interrupções e drivers de chip de interrupção**:

     - o desenvolvimento do núcleo de interrupções ocorre na branch irq/core

     - o desenvolvimento do driver de chip de interrupção também ocorre na branch
       irq/core, mas os patches geralmente são aplicados em uma árvore de mantenedor
       separada e depois agregados na irq/core

   - **Tempo, timers, timekeeping, NOHZ e drivers de chip relacionados**:

     - o desenvolvimento do timekeeping, núcleo clocksource, NTP e alarmtimer
       ocorre na branch timers/core, mas os patches geralmente são aplicados em
       uma árvore de mantenedor separada e depois agregados na timers/core

     - o desenvolvimento do driver clocksource/event ocorre na branch
       timers/core, mas os patches são em sua maioria aplicados em uma árvore de mantenedor
       separada e depois agregados na timers/core

   - **Núcleo de contadores de desempenho, suporte a arquitetura e ferramentas**:

     - o desenvolvimento do núcleo perf e suporte a arquitetura ocorre na
       branch perf/core

     - o desenvolvimento de ferramentas perf ocorre na árvore do mantenedor
       de ferramentas perf e é agregado à árvore tip.

   - **Núcleo de hotplug de CPU**

   - **Núcleo RAS**

     Em sua maioria, os patches RAS específicos para x86 são coletados na branch
     ras/core da árvore tip.

   - **Núcleo EFI**

     Desenvolvimento EFI na árvore git efi. Os patches coletados são
     agregados na branch efi/core da árvore tip.

   - **RCU**

     O desenvolvimento do RCU ocorre na árvore linux-rcu. As mudanças resultantes
     são agregadas na branch core/rcu da árvore tip.

   - **Vários componentes de código do núcleo**:

       - debugobjects

       - objtool

       - partes e peças aleatórias


Notas de submissão de patch
---------------------------

Selecionando a árvore/branch
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Em geral, o desenvolvimento contra o head da branch master da árvore tip é
adequado, mas para os subsistemas que são mantidos separadamente, possuem sua
própria árvore git e são apenas agregados na árvore tip, o desenvolvimento deve
ocorrer contra a árvore ou branch do subsistema relevante.

Correções de bugs que visam a mainline devem sempre ser aplicáveis contra a
árvore do kernel mainline. Potenciais conflitos contra mudanças que já estão
na fila da árvore tip são resolvidos pelos mantenedores.

Assunto do patch
^^^^^^^^^^^^^^^^

O formato preferido da árvore tip para prefixos de assunto do patch é
'subsys/component:', ex. 'x86/apic:', 'x86/mm/fault:', 'sched/fair:',
'genirq/core:'. Por favor, não use nomes de arquivos ou caminhos de arquivos completos como
prefixo. 'git log path/to/file' deve lhe dar uma dica razoável na maioria
dos casos.

A descrição condensada do patch na linha de assunto deve começar com uma
letra maiúscula e deve ser escrita em tom imperativo.


Changelog
^^^^^^^^^

As regras gerais sobre changelogs no :ref:`Guia de submissão de patches
<pt_BR_describe_changes>`, se aplicam.

Os mantenedores da árvore tip valorizam seguir essas regras, especialmente no
pedido para escrever changelogs no modo imperativo e não personificando
o código ou sua execução. Isso não é apenas um capricho dos
mantenedores. Changelogs escritos em palavras abstratas são mais precisos e
tendem a ser menos confusos do que aqueles escritos em forma de romances.

Também é útil estruturar o changelog em vários parágrafos e não
juntar tudo em um só. Uma boa estrutura é explicar
o contexto, o problema e a solução em parágrafos separados e nesta
ordem.

Exemplos para ilustração:

  Exemplo 1::

    x86/intel_rdt/mbm: Corrigir o manipulador de overflow do MBM durante hot cpu

    Quando uma CPU está morrendo, cancelamos o worker e agendamos um novo worker em uma
    CPU diferente no mesmo domínio. Mas se o timer já está prestes a
    expirar (digamos 0.99s) então essencialmente dobramos o intervalo.

    Modificamos o tratamento de hot cpu para cancelar o trabalho atrasado na cpu
    que está morrendo e executar o worker imediatamente em uma cpu diferente no mesmo domínio. Não
    fazemos o flush do worker porque o worker de overflow do MBM reagenda o
    worker na mesma CPU e escaneia a domain->cpu_mask para obter o ponteiro
    do domínio.

  Versão melhorada::

    x86/intel_rdt/mbm: Corrigir o manipulador de overflow do MBM durante hotplug de CPU

    Quando uma CPU está morrendo, o worker de overflow é cancelado e reagendado em uma
    CPU diferente no mesmo domínio. Mas se o timer já estiver prestes a
    expirar isso essencialmente dobra o intervalo, o que pode resultar em um overflow
    não detectado.

    Cancele o worker de overflow e reagende-o imediatamente em uma CPU diferente
    no mesmo domínio. O trabalho também poderia sofrer um flush, mas isso iria
    reagendá-lo na mesma CPU.

  Exemplo 2::

    time: POSIX CPU timers: Garantir que a variável seja inicializada

    Se cpu_timer_sample_group retornar -EINVAL, ela não terá escrito em
    *sample. Checar o valor de retorno de cpu_timer_sample_group previne o
    uso potencial de um valor não inicializado de now no bloco seguinte.
    Dado um clock_idx inválido, o código anterior poderia caso contrário sobrescrever
    *oldval de maneira indefinida. Isso agora é prevenido. Também exploramos
    o curto-circuito do && para amostrar o timer apenas se o resultado for
    realmente usado para atualizar *oldval.

  Versão melhorada::

    posix-cpu-timers: Tornar set_process_cpu_timer() mais robusto

    Como o valor de retorno de cpu_timer_sample_group() não é checado,
    compiladores e checadores estáticos podem legitimamente avisar sobre um uso potencial
    da variável não inicializada 'now'. Isso não é um problema de tempo de execução pois todos
    os locais de chamada passam ids de clock válidos.

    Além disso, cpu_timer_sample_group() é invocado incondicionalmente mesmo quando o
    resultado não é usado porque *oldval é NULL.

    Torne a invocação condicional e cheque o valor de retorno.

  Exemplo 3::

    A entidade também pode ser usada para outros propósitos.

    Vamos renomeá-la para ser mais genérica.

  Versão melhorada::

    A entidade também pode ser usada para outros propósitos.

    Renomeie para ser mais genérica.


Para cenários complexos, especialmente condições de corrida (race conditions) e problemas
de ordenação de memória, é valioso descrever o cenário com uma tabela que mostra
o paralelismo e a ordem temporal dos eventos. Aqui está um exemplo::

    CPU0                            CPU1
    free_irq(X)                     interrupt X
                                    spin_lock(desc->lock)
                                    wake irq thread()
                                    spin_unlock(desc->lock)
    spin_lock(desc->lock)
    remove action()
    shutdown_irq()
    release_resources()             thread_handler()
    spin_unlock(desc->lock)           access released resources.
                                      ^^^^^^^^^^^^^^^^^^^^^^^^^
    synchronize_irq()

O Lockdep fornece uma saída útil semelhante para descrever um possível cenário
de deadlock::

    CPU0                                    CPU1
    rtmutex_lock(&rcu->rt_mutex)
      spin_lock(&rcu->rt_mutex.wait_lock)
                                            local_irq_disable()
                                            spin_lock(&timer->it_lock)
                                            spin_lock(&rcu->mutex.wait_lock)
    --> Interrupt
        spin_lock(&timer->it_lock)

Referências a funções em changelogs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Quando uma função é mencionada no changelog, seja no corpo do texto ou na
linha de assunto, por favor use o formato 'nome_da_funcao()'. Omitir os
parênteses após o nome da função pode ser ambíguo::

  Subject: subsys/component: Make reservation_count static

  reservation_count is only used in reservation_stats. Make it static.

A variante com parênteses é mais precisa::

  Subject: subsys/component: Make reservation_count() static

  reservation_count() is only called from reservation_stats(). Make it
  static.


Backtraces em changelogs
^^^^^^^^^^^^^^^^^^^^^^^^

Veja :ref:`pt_BR_backtraces`.

Ordenação das tags de commit
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Para ter uma visão uniforme das tags de commit, os mantenedores da tip usam o
seguinte esquema de ordenação de tags:

 - Fixes: 12+char-SHA1 ("sub/sys: Original subject line")

   Uma tag Fixes deve ser adicionada mesmo para alterações que não precisam ser
   portadas de volta (backported) para kernels estáveis, ou seja, quando abordar um
   problema recém-introduzido que afeta apenas a árvore tip ou o head atual da linha principal (mainline). Estas tags
   são úteis para identificar o commit original e são muito mais valiosas
   do que mencionar de forma proeminente o commit que introduziu um problema no
   próprio texto do changelog, porque elas podem ser automaticamente
   extraídas.

   O exemplo a seguir ilustra a diferença::

     Commit

       abcdef012345678 ("x86/xxx: Replace foo with bar")

     deixou uma instância não utilizada da variável foo. Remova-a.

     Signed-off-by: J.Dev <j.dev@mail>

   Por favor, diga em vez disso::

     A recente substituição de foo por bar deixou uma instância não utilizada da
     variável foo. Remova-a.

     Fixes: abcdef012345678 ("x86/xxx: Replace foo with bar")
     Signed-off-by: J.Dev <j.dev@mail>

   O último coloca as informações sobre o patch em foco e
   as complementa com a referência ao commit que introduziu o problema,
   em vez de colocar o foco no commit original em primeiro lugar.

 - Reported-by: ``Reporter <reporter@mail>``

 - Closes: ``URL or Message-ID of the bug report this is fixing``

 - Originally-by: ``Original author <original-author@mail>``

 - Suggested-by: ``Suggester <suggester@mail>``

 - Co-developed-by: ``Co-author <co-author@mail>``

   Signed-off-by: ``Co-author <co-author@mail>``

   Note que Co-developed-by e Signed-off-by do(s) co-autor(es) devem
   vir em pares.

 - Signed-off-by: ``Author <author@mail>``

   O primeiro Signed-off-by (SOB) após o último par Co-developed-by/SOB é o
   SOB do autor, ou seja, a pessoa marcada como autora pelo git.

 - Signed-off-by: ``Patch handler <handler@mail>``

   SOBs após o SOB do autor são de pessoas que lidam e transportam
   o patch, mas não estiveram envolvidas no desenvolvimento. As cadeias de SOB devem
   refletir a rota **real** que um patch tomou conforme foi propagado para nós,
   com a primeira entrada de SOB sinalizando a autoria principal de um único
   autor. Acks devem ser dados como linhas Acked-by e aprovações de revisão
   como linhas Reviewed-by.

   Se o manipulador fez modificações no patch ou no changelog, então
   isso deve ser mencionado **após** o texto do changelog e **acima**
   de todas as tags de commit no seguinte formato::

     ... o texto do changelog termina.

     [ handler: Substituiu foo por bar e atualizou o changelog ]

     First-tag: .....

   Observe as duas novas linhas vazias que separam o texto do changelog e as
   tags de commit daquele aviso.

   Se um patch for enviado para a lista de discussão por um manipulador, então o autor tem
   que ser notado na primeira linha do changelog com::

     From: Author <author@mail>

     O texto do changelog começa aqui....

   assim a autoria é preservada. A linha 'From:' tem que ser seguida
   por uma nova linha vazia. Se essa linha 'From:' estiver faltando, então o patch
   seria atribuído à pessoa que o enviou (transportou, manipulou).
   A linha 'From:' é automaticamente removida quando o patch é aplicado
   e não aparece no changelog final do git. Ela meramente afeta
   a informação de autoria do commit resultante do Git.

 - Tested-by: ``Tester <tester@mail>``

 - Reviewed-by: ``Reviewer <reviewer@mail>``

 - Acked-by: ``Acker <acker@mail>``

 - Cc: ``cc-ed-person <person@mail>``

   Se o patch deve ser portado para stable, então por favor adicione uma tag '``Cc:
   stable@vger.kernel.org``', mas não coloque em Cc o stable ao enviar o seu
   e-mail.

 - Link: ``https://link/to/information``

   Para se referir a um e-mail postado nas listas de discussão do kernel, por favor
   use o URL de redirecionamento lore.kernel.org::

     Link: https://lore.kernel.org/email-message-id@here

   Esta URL deve ser usada ao se referir a tópicos de lista de discussão relevantes,
   conjuntos de patches relacionados, ou outras threads de discussão notáveis.
   Uma maneira conveniente de associar os trailers ``Link:`` com a mensagem de commit
   é usar a notação de colchetes semelhante ao markdown, por exemplo::

     A similar approach was attempted before as part of a different
     effort [1], but the initial implementation caused too many
     regressions [2], so it was backed out and reimplemented.

     Link: https://lore.kernel.org/some-msgid@here # [1]
     Link: https://bugzilla.example.org/bug/12345  # [2]

   Você também pode usar os trailers ``Link:`` para indicar a origem do
   patch ao aplicá-lo em sua árvore git. Neste caso, por favor use o
   domínio dedicado ``patch.msgid.link`` em vez de ``lore.kernel.org``.
   Esta prática torna possível que as ferramentas automatizadas identifiquem
   qual link usar para recuperar o envio do patch original. Por
   exemplo::

     Link: https://patch.msgid.link/patch-source-message-id@here

Por favor não use tags combinadas, ex. ``Reported-and-tested-by``, pois
elas apenas complicam a extração automatizada de tags.


Links para documentação
^^^^^^^^^^^^^^^^^^^^^^^

Fornecer links para a documentação no changelog é uma grande ajuda para depuração e
análise posteriores. Infelizmente, os URLs costumam quebrar muito rapidamente
porque as empresas reestruturam seus sites frequentemente. Exceções não 'voláteis'
incluem o Intel SDM e o AMD APM.

Portanto, para documentos 'voláteis', por favor crie uma entrada no bugzilla do kernel
https://bugzilla.kernel.org e anexe uma cópia desses documentos
à entrada do bugzilla. Finalmente, forneça o URL da entrada do bugzilla no
changelog.

Reenvio de patch ou lembretes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Veja :ref:`pt_BR_resend_reminders`.

Janela de merge
^^^^^^^^^^^^^^^

Por favor, não espere que os patches sejam revisados ou mesclados pelos mantenedores da árvore tip
em torno ou durante a janela de merge. As árvores ficam fechadas
para todos, exceto correções urgentes, durante esse tempo. Elas reabrem assim que a janela de merge
fecha e um novo kernel -rc1 é lançado.

Grandes séries devem ser enviadas em estado mesclável (mergeable state) *pelo* *menos* uma semana
antes da janela de merge abrir. Exceções são feitas para correções de bugs e
*às vezes* para pequenos drivers independentes para novos hardwares ou patches minimamente
invasivos para ativação de hardware.

Durante a janela de merge, os mantenedores se concentram em seguir as
alterações upstream, corrigir problemas resultantes da janela de merge, coletar correções de bugs, e
se permitir um respiro. Por favor, respeite isso.

Os chamados branches _urgent_ serão mesclados na linha principal (mainline) durante a
fase de estabilização de cada versão.


Git
^^^

Os mantenedores da árvore tip aceitam pull requests do git de mantenedores que fornecem
alterações de subsistema para agregação na árvore tip.

Pull requests para novos envios de patches normalmente não são aceitos e não
substituem o envio adequado de patch para a lista de discussão. O principal motivo para
isso é que o fluxo de trabalho de revisão é baseado em e-mail.

Se você enviar uma série maior de patches, é útil fornecer um branch git
em um repositório privado que permita que pessoas interessadas façam pull da
série facilmente para testes. A maneira usual de oferecer isso é uma URL do git na carta de apresentação (cover letter)
da série de patches.

Testes
^^^^^^

O código deve ser testado antes de ser enviado para os mantenedores da árvore tip. Qualquer coisa
além de alterações menores deve ser construída, inicializada e testada com
opções abrangentes (e pesadas) de depuração do kernel ativadas.

Essas opções de depuração podem ser encontradas em kernel/configs/x86_debug.config
e podem ser adicionadas a uma configuração de kernel existente executando:

	make x86_debug.config

Algumas dessas opções são específicas do x86 e podem ser deixadas de fora ao testar
em outras arquiteturas.

.. _pt_BR_maintainer-tip-coding-style:

Notas de estilo de código
-------------------------

Estilo de comentário
^^^^^^^^^^^^^^^^^^^^

Frases em comentários começam com uma letra maiúscula.

Comentários de linha única::

	/* Este é um comentário de linha única */

Comentários de várias linhas::

	/*
	 * This is a properly formatted
	 * multi-line comment.
	 *
	 * Larger multi-line comments should be split into paragraphs.
	 */

Sem comentários no fim da linha (veja abaixo):

  Por favor, abstenha-se de usar comentários no fim da linha. Comentários no fim da linha atrapalham o
  fluxo de leitura em quase todos os contextos, mas especialmente em código::

	if (somecondition_is_true) /* Não coloque um comentário aqui */
		dostuff(); /* Nem aqui */

	seed = MAGIC_CONSTANT; /* Nem aqui */

  Use comentários independentes em vez disso::

	/* Esta condição não é óbvia sem um comentário */
	if (somecondition_is_true) {
		/* Isso realmente precisa ser documentado */
		dostuff();
	}

	/* Esta inicialização mágica precisa de um comentário. Talvez não? */
	seed = MAGIC_CONSTANT;

  Use o estilo C++, comentários no fim da linha ao documentar structs em headers para
  alcançar um layout mais compacto e melhor legibilidade::

        // eax
        u32     x2apic_shift    :  5, // Número de bits para deslocar o ID APIC para a direita
                                      // para o ID de topologia no próximo nível
                                : 27; // Reservado
        // ebx
        u32     num_processors  : 16, // Número de processadores no nível atual
                                : 16; // Reservado

  versus::

	/* eax */
	        /*
	         * Número de bits para deslocar o ID APIC para a direita para o ID de topologia
	         * no próximo nível
	         */
         u32     x2apic_shift    :  5,
		 /* Reservado */
				 : 27;

	/* ebx */
		/* Número de processadores no nível atual */
	u32     num_processors  : 16,
		/* Reservado */
				: 16;

Comente as coisas importantes:

  Comentários devem ser adicionados onde a operação não é óbvia. Documentar
  o óbvio é apenas uma distração::

	/* Decrementa o refcount e verifica por zero */
	if (refcount_dec_and_test(&p->refcnt)) {
		do;
		lots;
		of;
		magic;
		things;
	}

  Em vez disso, os comentários devem explicar os detalhes não óbvios e documentar
  as restrições::

	if (refcount_dec_and_test(&p->refcnt)) {
		/*
		 * Explicação muito boa de por que as coisas mágicas abaixo
		 * precisam ser feitas, restrições de ordenação e locking,
		 * etc..
		 */
		do;
		lots;
		of;
		magic;
		/* Precisa ser a última operação porque ... */
		things;
	}

Comentários de documentação de função:

  Para documentar funções e seus argumentos por favor use o formato kernel-doc
  e não comentários de formato livre::

	/**
	 * magic_function - Faz muitas coisas mágicas
	 * @magic:	Ponteiro para os dados mágicos nos quais operar
	 * @offset:	Deslocamento no array de dados de @magic
	 *
	 * Explicação profunda das coisas misteriosas feitas com @magic junto
         * com a documentação dos valores de retorno.
	 *
	 * Note que os descritores de argumento acima estão dispostos
	 * de forma tabular.
	 */

  Isto se aplica especialmente a funções visíveis globalmente e funções
  inline em arquivos de cabeçalho públicos. Pode ser um exagero usar o formato
  kernel-doc para cada função (estática) que precisa de uma pequena explicação. O
  uso de nomes de funções descritivos frequentemente substitui esses pequenos comentários.
  Aplique o bom senso como sempre.


Documentando requisitos de locking
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  Documentar requisitos de locking é uma coisa boa, mas comentários não
  são necessariamente a melhor escolha. Em vez de escrever::

	/* Caller must hold foo->lock */
	void func(struct foo *foo)
	{
		...
	}

  Por favor, use::

	void func(struct foo *foo)
	{
		lockdep_assert_held(&foo->lock);
		...
	}

  Em kernels PROVE_LOCKING, lockdep_assert_held() emite um aviso
  se o chamador não detém o lock. Comentários não podem fazer isso.

Regras de chaves
^^^^^^^^^^^^^^^^

Chaves devem ser omitidas apenas se a instrução que se segue a 'if', 'for',
'while' etc. for verdadeiramente uma única linha::

	if (foo)
		do_something();

O seguinte não é considerado uma instrução de linha única mesmo
que o C não exija chaves::

	for (i = 0; i < end; i++)
		if (foo[i])
			do_something(foo[i]);

Adicionar chaves ao redor do loop externo melhora o fluxo de leitura::

	for (i = 0; i < end; i++) {
		if (foo[i])
			do_something(foo[i]);
	}


Declarações de variáveis
^^^^^^^^^^^^^^^^^^^^^^^^

A ordem preferida das declarações de variáveis no início de uma
função é a ordem de árvore de abeto invertida (reverse fir tree order)::

	struct long_struct_name *descriptive_name;
	unsigned long foo, bar;
	unsigned int tmp;
	int ret;

O que está acima é mais rápido de analisar do que a ordem invertida::

	int ret;
	unsigned int tmp;
	unsigned long foo, bar;
	struct long_struct_name *descriptive_name;

E ainda mais do que uma ordem aleatória::

	unsigned long foo, bar;
	int ret;
	struct long_struct_name *descriptive_name;
	unsigned int tmp;

Também por favor tente agregar variáveis do mesmo tipo em uma única
linha. Não há sentido em desperdiçar espaço na tela::

	unsigned long a;
	unsigned long b;
	unsigned long c;
	unsigned long d;

É realmente suficiente fazer::

	unsigned long a, b, c, d;

Por favor, evite também introduzir divisões de linha em declarações de variáveis::

	struct long_struct_name *descriptive_name = container_of(bar,
						      struct long_struct_name,
	                                              member);
	struct foobar foo;

É muito melhor mover a inicialização para uma linha separada após as
declarações::

	struct long_struct_name *descriptive_name;
	struct foobar foo;

	descriptive_name = container_of(bar, struct long_struct_name, member);


Tipos de variáveis
^^^^^^^^^^^^^^^^^^

Por favor use os tipos u8, u16, u32, u64 adequados para variáveis que são destinadas
a descrever hardware ou são usadas como argumentos para funções que acessam
hardware. Estes tipos definem claramente a largura em bits e evitam
truncamento, expansão e confusão entre 32/64 bits.

u64 também é recomendado em código que se tornaria ambíguo para kernels
de 32 bits quando 'unsigned long' fosse usado em vez disso. Embora em tais
situações 'unsigned long long' pudesse ser usado também, u64 é mais curto
e também mostra claramente que a operação requer uma largura de 64 bits
independente da CPU alvo.

Por favor use 'unsigned int' em vez de 'unsigned'.


Constantes
^^^^^^^^^^

Por favor, não use números (hexa)decimais literais em código ou inicializadores.
Ou use defines adequados que tenham nomes descritivos ou considere usar
um enum.


Declarações e inicializadores de struct
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

As declarações de struct devem alinhar os nomes dos membros da struct de forma
tabular::

	struct bar_order {
		unsigned int	guest_id;
		int		ordered_item;
		struct menu	*menu;
	};

Por favor, evite documentar os membros da struct dentro da declaração, pois
isso frequentemente resulta em comentários formatados de maneira estranha e os membros da struct
ficam ofuscados::

	struct bar_order {
		unsigned int	guest_id; /* ID único do convidado */
		int		ordered_item;
		/* Ponteiro para uma instância de menu que contém todas as bebidas */
		struct menu	*menu;
	};

Em vez disso, por favor considere usar o formato kernel-doc em um comentário precedendo
a declaração da struct, que é mais fácil de ler e tem a vantagem adicional
de incluir a informação na documentação do kernel, por exemplo, da
seguinte forma::


	/**
	 * struct bar_order - Descrição de um pedido de bar
	 * @guest_id:		ID único do convidado
	 * @ordered_item:	O número do item do menu
	 * @menu:		Ponteiro para o menu do qual o item
	 *  			foi pedido
	 *
	 * Informação suplementar para usar a struct.
	 *
	 * Note que os descritores dos membros da struct acima estão dispostos
	 * de forma tabular.
	 */
	struct bar_order {
		unsigned int	guest_id;
		int		ordered_item;
		struct menu	*menu;
	};

Inicializadores de struct estáticos devem usar inicializadores C99 e também devem ser
alinhados de forma tabular::

	static struct foo statfoo = {
		.a		= 0,
		.plain_integer	= CONSTANT_DEFINE_OR_ENUM,
		.bar		= &statbar,
	};

Note que embora a sintaxe C99 permita a omissão da vírgula final,
nós recomendamos o uso de uma vírgula na última linha porque isso torna
o reordenamento e a adição de novas linhas mais fáceis, e também torna tais
patches futuros ligeiramente mais fáceis de ler.

Quebras de linha
^^^^^^^^^^^^^^^^

Restringir o comprimento da linha a 80 caracteres torna código profundamente indentado difícil de
ler. Considere dividir o código em funções auxiliares para evitar quebra de
linha excessiva.

A regra de 80 caracteres não é uma regra estrita, então por favor use bom senso ao
quebrar linhas. Especialmente strings de formato nunca devem ser divididas.

Ao dividir declarações de funções ou chamadas de funções, então por favor alinhe
o primeiro argumento na segunda linha com o primeiro argumento na primeira
linha::

  static int long_function_name(struct foobar *barfoo, unsigned int id,
				unsigned int offset)
  {

	if (!id) {
		ret = longer_function_name(barfoo, DEFAULT_BARFOO_ID,
					   offset);
	...

Namespaces
^^^^^^^^^^

Namespaces de funções/variáveis melhoram a legibilidade e permitem
grepping fácil. Estes namespaces são prefixos de string para nomes
de funções e variáveis visíveis globalmente, incluindo inlines. Estes prefixos devem
combinar o subsistema e o nome do componente como 'x86_comp\_',
'sched\_', 'irq\_', e 'mutex\_'.

Isso também inclui funções estáticas de escopo de arquivo que são imediatamente colocadas
em templates de driver visíveis globalmente - é útil que esses símbolos
também carreguem um bom prefixo, para legibilidade do backtrace.

Prefixos de namespace podem ser omitidos para funções e variáveis
estáticas locais. Funções verdadeiramente locais, chamadas apenas por outras funções locais,
podem ter nomes descritivos mais curtos - nossa preocupação principal é a facilidade de grepping
e a legibilidade do backtrace.

Por favor note que os prefixos 'xxx_vendor\_' e 'vendor_xxx\_' não são
úteis para funções estáticas em arquivos específicos de fornecedores. Afinal,
já está claro que o código é específico do fornecedor. Além disso, nomes
de fornecedores devem ser apenas para funcionalidades verdadeiramente específicas de fornecedores.

Como sempre, aplique o bom senso e vise a consistência e a legibilidade.


Notificações de commit
----------------------

A árvore tip é monitorada por um bot por novos commits. O bot envia um email
para cada novo commit para uma lista de discussão dedicada
(``linux-tip-commits@vger.kernel.org``) e coloca em Cc todas as pessoas que são
mencionadas em uma das tags de commit. Ele usa o ID da mensagem de email da
tag Link no final da lista de tags para definir o cabeçalho de email In-Reply-To para que
a mensagem seja encadeada corretamente com o email de submissão do patch.

Os mantenedores e submantenedores tip tentam responder ao remetente
ao fazer o merge de um patch, mas às vezes eles esquecem ou isso não se encaixa no
fluxo de trabalho do momento. Embora a mensagem do bot seja puramente mecânica, ela
também implica em um 'Obrigado! Aplicado.'.
